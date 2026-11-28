#include <application.hpp>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <iterator>
#include <application_limits.hpp>
#include <memory>
#include <numbers>
#include <thread>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/weakref.h>
#include <wx/wx.h>

wxString convertToWxString(std::string_view value) {
    return wxString::FromUTF8(value.data(), value.size());
}

std::string convertToUtf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}

template <class Catalog, class Id>
void fillChoice(wxChoice& choice, const Catalog& entries, Id selected) {
    choice.Clear();
    for (std::size_t i = 0; i < std::size(entries); ++i) {
        const auto& entry = entries[i];
        const auto index = choice.Append(convertToWxString(entry.displayName));
        if (entry.id == selected) {
            choice.SetSelection(static_cast<int>(index));
        }
    }
}

template <class Catalog>
auto readChoice(const wxChoice& choice, const Catalog& entries, std::string_view field)
    -> Result<decltype(entries[0].id)> {
    const auto index = choice.GetSelection();
    if (index < 0 || static_cast<std::size_t>(index) >= std::size(entries)) {
        return failure(GraphErrorCode::InvalidParameters, "Select a valid " + std::string(field) + ".");
    }
    return entries[static_cast<std::size_t>(index)].id;
}

struct TableColumn {
    long index;
    std::string_view caption;
    int width;
};

// wxWidgets владеет дочерними окнами. Поля хранят слабые ссылки с отслеживанием их удаления
template <class T, class... Args>
wxWeakRef<T> createControl(wxWindow& parent, Args&&... args) {
    auto owned = std::make_unique<T>(&parent, std::forward<Args>(args)...);
    wxWeakRef<T> reference(owned.get());
    (void)owned.release();
    return reference;
}

template <class TSizer>
void addSizer(wxSizer& parent,
              std::unique_ptr<TSizer> child,
              int proportion = 0,
              int flags = wxEXPAND,
              int border = 0) {
    parent.Add(child.get(), proportion, flags, border);
    (void)child.release();
}

void addLabel(wxWindow& parent, wxSizer& sizer, const wxString& value, int flags = wxRIGHT) {
    auto item = createControl<wxStaticText>(parent, wxID_ANY, value);
    sizer.Add(item.get(), 0, flags, 8);
}

template <class T>
Result<T> parseNumber(wxString value, std::string_view field) {
    auto inputText = convertToUtf8(value.Trim().Trim(false));
    T parsed{};
    const auto result = std::from_chars(inputText.data(), inputText.data() + inputText.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != inputText.data() + inputText.size()) {
        return failure(GraphErrorCode::InvalidParameters, "Invalid " + std::string(field) + ".");
    }
    return parsed;
}

struct GenerationControls {
    wxWeakRef<wxSpinCtrl> count;
    wxWeakRef<wxChoice> topology;
    wxWeakRef<wxTextCtrl> edgeProbability, seed;

    void create(wxWindow& parent, wxSizer& sizer) {
        const GenerationParameters defaults;
        auto fields = std::make_unique<wxFlexGridSizer>(2, 6, 6);
        fields->AddGrowableCol(1);
        addLabel(parent, *fields, "Vertices / max. size", wxALIGN_CENTER_VERTICAL | wxRIGHT);
        count = createControl<wxSpinCtrl>(parent, wxID_ANY);
        count->SetRange(0, static_cast<int>(ApplicationLimits::maxGeneratedVertices));
        count->SetValue(static_cast<int>(defaults.vertexCount));
        fields->Add(count.get(), 1, wxEXPAND);
        addLabel(parent, *fields, "Topology", wxALIGN_CENTER_VERTICAL | wxRIGHT);
        topology = createControl<wxChoice>(parent, wxID_ANY);
        fillChoice(*topology, topologyCatalog, defaults.topology);
        fields->Add(topology.get(), 1, wxEXPAND);
        addLabel(parent, *fields, "Edge probability", wxALIGN_CENTER_VERTICAL | wxRIGHT);
        edgeProbability = createControl<wxTextCtrl>(
            parent, wxID_ANY, convertToWxString(std::format("{}", defaults.edgeProbability)));
        fields->Add(edgeProbability.get(), 1, wxEXPAND);
        addLabel(parent, *fields, "Seed", wxALIGN_CENTER_VERTICAL | wxRIGHT);
        seed = createControl<wxTextCtrl>(parent, wxID_ANY, convertToWxString(std::to_string(defaults.seed)));
        fields->Add(seed.get(), 1, wxEXPAND);
        addSizer(sizer, std::move(fields), 0, wxEXPAND | wxBOTTOM, 8);
        topology->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { updateRandomControls(); });
        updateRandomControls();
    }

    void updateRandomControls() {
        const auto selected = readChoice(*topology, topologyCatalog, "topology");
        bool enabled = false;
        if (selected) {
            const auto entry = findTopologyDescriptor(*selected);
            enabled = entry && entry->get().usesRandomParameters;
        }
        edgeProbability->Enable(enabled);
        seed->Enable(enabled);
    }

    Result<GenerationParameters> read() const {
        auto selectedTopology = readChoice(*topology, topologyCatalog, "topology");
        if (!selectedTopology) {
            return std::unexpected(selectedTopology.error());
        }
        auto entry = findTopologyDescriptor(*selectedTopology);
        if (!entry) {
            return std::unexpected(entry.error());
        }
        GenerationParameters parameters;
        parameters.vertexCount = static_cast<std::size_t>(count->GetValue());
        parameters.topology = *selectedTopology;
        if (entry->get().usesRandomParameters) {
            auto parsedEdgeProbability = parseNumber<double>(edgeProbability->GetValue(), "probability");
            auto parsedSeed = parseNumber<std::uint32_t>(seed->GetValue(), "seed");
            if (!parsedEdgeProbability) {
                return std::unexpected(parsedEdgeProbability.error());
            }
            if (!parsedSeed) {
                return std::unexpected(parsedSeed.error());
            }
            parameters.edgeProbability = *parsedEdgeProbability;
            parameters.seed = *parsedSeed;
        }
        return parameters;
    }
};

class GraphCanvas final : public wxPanel {
    static constexpr std::size_t maxDrawnVertices = 200;
    static constexpr int vertexRadius = 15;
    static constexpr double hueStep = 0.61803398875;
    static constexpr double colorSaturation = 0.42;
    static constexpr double colorBrightness = 0.96;

    const GraphApplication& application_;
    const bool& busy_;

    struct Position {
        int x, y;
    };

    static wxColour colorForIndex(std::size_t index) {
        const auto rgb = wxImage::HSVtoRGB(wxImage::HSVValue(
            std::fmod(static_cast<double>(index) * hueStep, 1.0), colorSaturation, colorBrightness));
        return wxColour(rgb.red, rgb.green, rgb.blue);
    }

    void paint(wxPaintEvent&) {
        wxAutoBufferedPaintDC dc(this);
        dc.SetBackground(wxBrush(wxColour(250, 251, 253)));
        dc.Clear();
        dc.SetTextForeground(wxColour(40, 45, 55));
        if (busy_) {
            dc.DrawText("Working...", 18, 18);
            return;
        }
        const auto& graph = application_.graph();
        const auto count = graph.vertexCount();
        if (count == 0) {
            dc.DrawText("Add vertices or generate a graph to begin.", 18, 18);
            return;
        }
        if (count > maxDrawnVertices) {
            dc.DrawText(wxString::Format(
                            "Drawing is available for up to %zu vertices.\n"
                            "All vertices and neighbors are listed in the table below.",
                            maxDrawnVertices),
                        18,
                        18);
            return;
        }
        const auto size = GetClientSize();
        const auto radius = std::max(10, std::min(size.x, size.y) / 2 - 36);
        ArraySequence<Position> positions;
        auto reserved = positions.reserve(count);
        if (!reserved) {
            dc.DrawText(convertToWxString(formatError(reserved.error())), 18, 18);
            return;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const double angle =
                2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(count) -
                std::numbers::pi / 2;
            positions.push_back(
                {size.x / 2 + (count == 1 ? 0 : static_cast<int>(radius * std::cos(angle))),
                 size.y / 2 + (count == 1 ? 0 : static_cast<int>(radius * std::sin(angle)))}).value();
        }
        const auto ids = graph.vertexIds();
        dc.SetPen(wxPen(wxColour(175, 184, 198), 1));
        for (const auto id : graph.edgeIds()) {
            const auto& edge = graph.edge(id)->get();
            const auto first = *findVertexPositionById(ids, edge.firstVertexId);
            const auto second = *findVertexPositionById(ids, edge.secondVertexId);
            dc.DrawLine(positions[first].x, positions[first].y, positions[second].x, positions[second].y);
        }
        for (std::size_t i = 0; i < count; ++i) {
            dc.SetBrush(wxBrush(application_.coloring() ? colorForIndex(*application_.coloring()->color(ids[i]))
                                                        : wxColour(224, 232, 245)));
            dc.SetPen(wxPen(wxColour(75, 87, 110), 1));
            dc.DrawCircle(positions[i].x, positions[i].y, vertexRadius);
            const auto id = wxString::Format("%zu", ids[i]);
            const auto extent = dc.GetTextExtent(id);
            dc.DrawText(id, positions[i].x - extent.x / 2, positions[i].y - extent.y / 2);
        }
    }

public:
    GraphCanvas(wxWindow* parent, const GraphApplication& application, const bool& busy)
        : wxPanel(parent), application_(application), busy_(busy) {
        SetBackgroundStyle(wxBG_STYLE_PAINT);
        SetMinSize(wxSize(350, 260));
        Bind(wxEVT_PAINT, &GraphCanvas::paint, this);
        Bind(wxEVT_SIZE, [this](wxSizeEvent& event) {
            Refresh();
            event.Skip();
        });
    }
};

class VertexTable final : public wxListCtrl {
    enum Column : long { Vertex, Label, Degree, Color, Step, Neighbors };

    const GraphApplication& application_;
    const bool& busy_;
    ArraySequence<VertexId> vertexIds_;
    ArraySequence<std::size_t> processingSteps_;

    wxString OnGetItemText(long row, long column) const override {
        if (busy_) {
            return {};
        }
        const auto position = static_cast<std::size_t>(row);
        if (position >= vertexIds_.size()) return {};
        const auto id = vertexIds_[position];
        const auto& graph = application_.graph();
        if (!graph.containsVertex(id)) {
            return {};
        }
        const auto& neighbors = graph.neighbors(id)->get();
        switch (column) {
        case Column::Vertex:
            return wxString::Format("%zu", id);
        case Column::Label:
            return convertToWxString(graph.vertex(id)->get().data.label);
        case Column::Degree:
            return wxString::Format("%zu", neighbors.size());
        case Column::Color:
            return application_.coloring()
                       ? wxString::Format("C%zu", *application_.coloring()->color(id))
                       : "—";
        case Column::Step:
            return application_.coloring() ? wxString::Format("%zu", processingSteps_[position] + 1) : "—";
        case Column::Neighbors: {
            wxString list;
            for (std::size_t i = 0; i < neighbors.size(); ++i) {
                if (i) {
                    list += ", ";
                }
                list += wxString::Format("%zu", neighbors[i].neighborId);
            }
            return list;
        }
        default:
            return {};
        }
    }

public:
    VertexTable(wxWindow* parent, const GraphApplication& application, const bool& busy)
        : wxListCtrl(
              parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT | wxLC_VIRTUAL),
          application_(application), busy_(busy) {
        constexpr TableColumn columns[] = {
            {Column::Vertex, "Vertex", 75},
            {Column::Label, "Label", 160},
            {Column::Degree, "Degree", 75},
            {Column::Color, "Color", 75},
            {Column::Step, "Step", 75},
            {Column::Neighbors, "Neighbors", 400}};
        for (const auto& column : columns) {
            InsertColumn(column.index, convertToWxString(column.caption), wxLIST_FORMAT_LEFT, column.width);
        }
        SetMinSize(wxSize(350, 180));
    }

    void update() {
        vertexIds_ = application_.graph().vertexIds();
        const auto count = application_.graph().vertexCount();
        processingSteps_ = ArraySequence<std::size_t>(count, 0);
        if (application_.coloring()) {
            const auto& processingOrder = application_.coloring()->processingOrder;
            for (std::size_t i = 0; i < count; ++i) {
                processingSteps_[*findVertexPositionById(vertexIds_, processingOrder[i])] = i;
            }
        }
        SetItemCount(static_cast<long>(count));
        Refresh();
    }
};

class MainFrame final : public wxFrame {
    enum ComparisonColumn : long { Vertices, Edges, Algorithm, MedianTime, Colors };

    GraphApplication application_;
    bool busy_ = false;
    bool closeRequested_ = false;
    std::filesystem::path currentPath_;
    wxWeakRef<wxNotebook> notebook_;
    std::size_t comparisonPageIndex_{};
    wxWeakRef<wxPanel> graphPanel_, comparisonPanel_;
    wxWeakRef<GraphCanvas> canvas_;
    wxWeakRef<VertexTable> vertexTable_;
    wxWeakRef<wxStaticText> summary_;
    wxWeakRef<wxTextCtrl> vertexLabel_, firstVertexIdInput_, secondVertexIdInput_;
    wxWeakRef<wxChoice> algorithm_;
    wxWeakRef<wxSpinCtrl> repetitionCount_, seriesPointCount_;
    wxWeakRef<wxListCtrl> benchmarkTable_;
    GenerationControls generationControls_, seriesGenerationControls_;
    std::jthread worker_;

    void showError(const GraphError& error) {
        wxMessageBox(convertToWxString(formatError(error)), "DOR3", wxOK | wxICON_ERROR, this);
    }

    template <class Action>
    void executeWithErrorHandling(Action&& action) {
        try {
            action();
        } catch (const std::exception& error) {
            showError({GraphErrorCode::Unexpected, error.what()});
        }
    }

    template <class Action>
    void addButton(wxWindow& parent, wxSizer& sizer, const wxString& caption, Action action) {
        auto item = createControl<wxButton>(parent, wxID_ANY, caption);
        sizer.Add(item.get(), 0, wxEXPAND | wxBOTTOM, 6);
        item->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) {
            executeWithErrorHandling(action);
        });
    }

    void refresh() {
        canvas_->Refresh();
        vertexTable_->update();
        wxString summary = wxString::Format("Vertices: %zu   Edges: %zu",
                                            application_.graph().vertexCount(),
                                            application_.graph().edgeCount());
        if (application_.coloring()) {
            summary +=
                wxString::Format("   Colors: %zu   Validated", application_.coloring()->colorCount);
        }
        summary_->SetLabel(summary);
        benchmarkTable_->DeleteAllItems();
        const auto& rows = application_.benchmarkResults();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const auto index = benchmarkTable_->InsertItem(static_cast<long>(i), wxEmptyString);
            benchmarkTable_->SetItem(index, ComparisonColumn::Vertices,
                                   wxString::Format("%zu", row.vertexCount));
            benchmarkTable_->SetItem(index, ComparisonColumn::Edges,
                                   wxString::Format("%zu", row.edgeCount));
            benchmarkTable_->SetItem(index, ComparisonColumn::Algorithm,
                                   convertToWxString(coloringAlgorithmName(row.algorithm)));
            benchmarkTable_->SetItem(index, ComparisonColumn::MedianTime,
                                   wxString::Format("%.3f", row.medianMicroseconds));
            benchmarkTable_->SetItem(index, ComparisonColumn::Colors,
                                   wxString::Format("%zu", row.colorCount));
        }
        SetStatusText(currentPath_.empty() ? "Ready" : convertToWxString(currentPath_.string()));
        SetTitle(application_.isModified() ? "DOR3 — Graph coloring *" : "DOR3 — Graph coloring");
        Layout();
    }

    void setActionsEnabled(bool enabled) {
        graphPanel_->Enable(enabled);
        comparisonPanel_->Enable(enabled);
        for (int id : {wxID_NEW, wxID_OPEN, wxID_SAVE, wxID_SAVEAS}) {
            GetMenuBar()->Enable(id, enabled);
        }
    }

    void setBusy(bool busy) {
        busy_ = busy;
        setActionsEnabled(!busy);
        canvas_->Refresh();
        vertexTable_->Refresh();
        SetStatusText(busy ? "Working..."
                           : currentPath_.empty() ? "Ready" : convertToWxString(currentPath_.string()));
    }

    void runBackgroundTask(std::function<Result<void>()> action,
                 bool showComparison = false,
                 std::function<void()> onSuccess = {}) {
        if (busy_) {
            return;
        }
        auto task =
            [this, action = std::move(action), showComparison, onSuccess = std::move(onSuccess)] {
                Result<void> result;
                try {
                    result = action();
                } catch (const std::exception& error) {
                    result = failure(GraphErrorCode::Unexpected, error.what());
                }
                CallAfter([this, result = std::move(result), showComparison, onSuccess] {
                    executeWithErrorHandling([&] {
                        setBusy(false);
                        if (!result) {
                            showError(result.error());
                        } else {
                            if (onSuccess) {
                                onSuccess();
                            }
                            if (showComparison) {
                                notebook_->SetSelection(comparisonPageIndex_);
                            }
                        }
                        refresh();
                        if (closeRequested_) {
                            closeRequested_ = false;
                            Close();
                        }
                    });
                });
            };
        try {
            setBusy(true);
            worker_ = std::jthread(std::move(task));
        } catch (...) {
            setBusy(false);
            throw;
        }
    }

    bool confirmReplacement() {
        return application_.graph().vertexCount() == 0 ||
               wxMessageBox(application_.isModified()
                                ? "Replace the current graph? Unsaved changes will be lost."
                                : "Replace the current graph?",
                            "DOR3",
                            wxYES_NO | wxNO_DEFAULT | wxICON_QUESTION,
                            this) == wxYES;
    }

    bool save(bool choosePath) {
        auto path = currentPath_;
        if (choosePath || path.empty()) {
            wxFileDialog dialog(this,
                                "Save graph",
                                "",
                                "graph.graph",
                                "Graph files (*.graph)|*.graph|All files|*",
                                wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dialog.ShowModal() != wxID_OK) {
                return false;
            }
            path = convertToUtf8(dialog.GetPath());
        }
        auto result = application_.save(path);
        if (!result) {
            showError(result.error());
            return false;
        }
        currentPath_ = path;
        refresh();
        return true;
    }

    void open() {
        wxFileDialog dialog(this,
                            "Open graph",
                            "",
                            "",
                            "Graph files (*.graph)|*.graph|All files|*",
                            wxFD_OPEN | wxFD_FILE_MUST_EXIST);
        if (dialog.ShowModal() != wxID_OK || !confirmReplacement()) {
            return;
        }
        const auto path = std::filesystem::path(convertToUtf8(dialog.GetPath()));
        runBackgroundTask(
            [this, path] {
                return application_.open(path);
            },
            false,
            [this, path] {
                currentPath_ = path;
            });
    }

    void changeEdge(bool remove) {
        auto first = parseNumber<VertexId>(firstVertexIdInput_->GetValue(), "first vertex");
        auto second = parseNumber<VertexId>(secondVertexIdInput_->GetValue(), "second vertex");
        if (!first) {
            showError(first.error());
            return;
        }
        if (!second) {
            showError(second.error());
            return;
        }
        const auto result = remove ? application_.removeEdge(*first, *second)
                                   : application_.addEdge(*first, *second);
        if (!result) {
            showError(result.error());
        } else {
            refresh();
        }
    }

    void buildGraphPage(wxNotebook& notebook) {
        graphPanel_ = createControl<wxPanel>(notebook, wxID_ANY);
        auto root = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        auto tools = std::make_unique<wxBoxSizer>(wxVERTICAL);
        addLabel(*graphPanel_, *tools, "Manual editing");
        vertexLabel_ = createControl<wxTextCtrl>(*graphPanel_, wxID_ANY, "Vertex");
        tools->Add(vertexLabel_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        auto vertexActions = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        addButton(*graphPanel_, *vertexActions, "Add vertex", [this] {
            auto result = application_.addVertex(convertToUtf8(vertexLabel_->GetValue()));
            if (!result) {
                showError(result.error());
            } else {
                refresh();
            }
        });
        addButton(*graphPanel_, *vertexActions, "Set vertex label", [this] {
            auto id = parseNumber<VertexId>(firstVertexIdInput_->GetValue(), "vertex ID");
            if (!id) {
                showError(id.error());
                return;
            }
            auto result = application_.setVertexData(*id, convertToUtf8(vertexLabel_->GetValue()));
            if (!result) {
                showError(result.error());
            } else {
                refresh();
            }
        });
        addSizer(*tools, std::move(vertexActions));
        addLabel(*graphPanel_, *tools, "Vertex actions use the first ID");
        auto edge = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        firstVertexIdInput_ =
            createControl<wxTextCtrl>(*graphPanel_, wxID_ANY, "0", wxDefaultPosition, wxSize(90, -1));
        secondVertexIdInput_ =
            createControl<wxTextCtrl>(*graphPanel_, wxID_ANY, "1", wxDefaultPosition, wxSize(90, -1));
        edge->Add(firstVertexIdInput_.get(), 1, wxRIGHT, 6);
        edge->Add(secondVertexIdInput_.get(), 1);
        addSizer(*tools, std::move(edge), 0, wxEXPAND | wxBOTTOM, 6);
        addButton(*graphPanel_, *tools, "Add edge", [this] {
            changeEdge(false);
        });
        addButton(*graphPanel_, *tools, "Remove edge", [this] {
            changeEdge(true);
        });
        addButton(*graphPanel_, *tools, "Remove vertex", [this] {
            auto id = parseNumber<VertexId>(firstVertexIdInput_->GetValue(), "vertex ID");
            if (!id) {
                showError(id.error());
                return;
            }
            runBackgroundTask([this, id = *id] {
                return application_.removeVertex(id);
            });
        });
        tools->AddSpacer(10);
        addLabel(*graphPanel_, *tools, "Generate graph");
        tools->AddSpacer(8);
        generationControls_.create(*graphPanel_, *tools);
        addButton(*graphPanel_, *tools, "Generate", [this] {
            auto parameters = generationControls_.read();
            if (!parameters) {
                showError(parameters.error());
                return;
            }
            if (!confirmReplacement()) {
                return;
            }
            runBackgroundTask(
                [this, parameters = *parameters] {
                    return application_.generate(parameters);
                },
                false,
                [this] {
                    currentPath_.clear();
                });
        });
        tools->AddSpacer(10);
        addLabel(*graphPanel_, *tools, "Coloring algorithm");
        tools->AddSpacer(8);
        algorithm_ = createControl<wxChoice>(*graphPanel_, wxID_ANY);
        fillChoice(*algorithm_, coloringAlgorithmCatalog<Graph>, ColoringAlgorithm::Sequential);
        tools->Add(algorithm_.get(), 0, wxEXPAND | wxBOTTOM, 6);
        addButton(*graphPanel_, *tools, "Color graph", [this] {
            auto selectedAlgorithm = readChoice(*algorithm_, coloringAlgorithmCatalog<Graph>, "algorithm");
            if (!selectedAlgorithm) {
                showError(selectedAlgorithm.error());
                return;
            }
            runBackgroundTask([this, algorithm = *selectedAlgorithm] {
                return application_.computeColoring(algorithm);
            });
        });
        addButton(*graphPanel_, *tools, "Compare algorithms", [this] {
            const auto repetitionCount = static_cast<std::size_t>(repetitionCount_->GetValue());
            runBackgroundTask(
                [this, repetitionCount] {
                    return application_.compareAlgorithms(repetitionCount);
                },
                true);
        });
        auto note = createControl<wxStaticText>(*graphPanel_,
                                          wxID_ANY,
                                          "Vertex IDs are stable; gaps are allowed.\nGreedy methods do "
                                          "not guarantee\nthe minimum number of colors.");
        tools->Add(note.get(), 0, wxTOP, 8);
        addSizer(*root, std::move(tools), 0, wxEXPAND | wxALL, 12);
        auto view = std::make_unique<wxBoxSizer>(wxVERTICAL);
        summary_ = createControl<wxStaticText>(*graphPanel_, wxID_ANY, "");
        view->Add(summary_.get(), 0, wxBOTTOM, 8);
        canvas_ = createControl<GraphCanvas>(*graphPanel_, application_, busy_);
        view->Add(canvas_.get(), 3, wxEXPAND | wxBOTTOM, 8);
        vertexTable_ = createControl<VertexTable>(*graphPanel_, application_, busy_);
        view->Add(vertexTable_.get(), 2, wxEXPAND);
        addSizer(*root, std::move(view), 1, wxEXPAND | wxALL, 12);
        graphPanel_->SetSizer(root.release());
        notebook.AddPage(graphPanel_.get(), "Graph");
    }

    void buildComparisonPage(wxNotebook& notebook) {
        comparisonPanel_ = createControl<wxPanel>(notebook, wxID_ANY);
        auto root = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        auto tools = std::make_unique<wxBoxSizer>(wxVERTICAL);
        addLabel(*comparisonPanel_, *tools, "Comparison settings");
        tools->AddSpacer(8);
        addLabel(*comparisonPanel_, *tools, "Repetitions (median)");
        repetitionCount_ = createControl<wxSpinCtrl>(*comparisonPanel_, wxID_ANY);
        repetitionCount_->SetRange(1, static_cast<int>(ApplicationLimits::maxRepetitionCount));
        repetitionCount_->SetValue(static_cast<int>(BenchmarkDefaults::repetitionCount));
        tools->Add(repetitionCount_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        addButton(*comparisonPanel_, *tools, "Compare current graph", [this] {
            const auto repetitionCount = static_cast<std::size_t>(repetitionCount_->GetValue());
            runBackgroundTask(
                [this, repetitionCount] {
                    return application_.compareAlgorithms(repetitionCount);
                },
                true);
        });
        tools->AddSpacer(12);
        addLabel(*comparisonPanel_, *tools, "Series for plotting");
        tools->AddSpacer(8);
        seriesGenerationControls_.create(*comparisonPanel_, *tools);
        addLabel(*comparisonPanel_, *tools, "Number of sizes");
        seriesPointCount_ = createControl<wxSpinCtrl>(*comparisonPanel_, wxID_ANY);
        seriesPointCount_->SetRange(1, static_cast<int>(ApplicationLimits::maxSeriesPointCount));
        seriesPointCount_->SetValue(static_cast<int>(BenchmarkDefaults::seriesPointCount));
        tools->Add(seriesPointCount_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        addButton(*comparisonPanel_, *tools, "Compare size series", [this] {
            auto parameters = seriesGenerationControls_.read();
            if (!parameters) {
                showError(parameters.error());
                return;
            }
            const auto seriesPointCount = static_cast<std::size_t>(seriesPointCount_->GetValue());
            const auto repetitionCount = static_cast<std::size_t>(repetitionCount_->GetValue());
            runBackgroundTask(
                [this, parameters = *parameters, seriesPointCount, repetitionCount] {
                    return application_.compareSizeSeries(parameters, seriesPointCount, repetitionCount);
                },
                true);
        });
        addButton(*comparisonPanel_, *tools, "Export CSV...", [this] {
            wxFileDialog dialog(this,
                                "Export comparison",
                                "",
                                "comparison.csv",
                                "CSV files (*.csv)|*.csv",
                                wxFD_SAVE | wxFD_OVERWRITE_PROMPT);
            if (dialog.ShowModal() != wxID_OK) {
                return;
            }
            const auto result =
                exportBenchmark(application_.benchmarkResults(), convertToUtf8(dialog.GetPath()));
            if (!result) {
                showError(result.error());
            } else {
                SetStatusText("Comparison exported.");
            }
        });
        auto note =
            createControl<wxStaticText>(*comparisonPanel_,
                                  wxID_ANY,
                                  "All methods use the same graph.\nIncludes ordering and "
                                  "allocation;\nvalidation runs outside the timer.\n"
                                  "All displayed results are validated.\n\nSeries keeps "
                                  "the current graph.\nRows remain until the next comparison.");
        tools->Add(note.get(), 0, wxTOP, 10);
        addSizer(*root, std::move(tools), 0, wxEXPAND | wxALL, 12);
        benchmarkTable_ = createControl<wxListCtrl>(
            *comparisonPanel_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
        constexpr TableColumn columns[] = {
            {ComparisonColumn::Vertices, "Vertices", 100},
            {ComparisonColumn::Edges, "Edges", 100},
            {ComparisonColumn::Algorithm, "Algorithm", 190},
            {ComparisonColumn::MedianTime, "Median (µs)", 100},
            {ComparisonColumn::Colors, "Colors", 100}};
        for (const auto& column : columns) {
            benchmarkTable_->InsertColumn(column.index, convertToWxString(column.caption), wxLIST_FORMAT_LEFT,
                                        column.width);
        }
        root->Add(benchmarkTable_.get(), 1, wxEXPAND | wxALL, 12);
        comparisonPanel_->SetSizer(root.release());
        comparisonPageIndex_ = notebook.GetPageCount();
        notebook.AddPage(comparisonPanel_.get(), "Algorithm comparison");
    }

public:
    MainFrame()
        : wxFrame(
              nullptr, wxID_ANY, "DOR3 — Graph coloring", wxDefaultPosition, wxSize(1200, 850)) {
        SetMinSize(wxSize(1040, 820));
        auto menuBar = std::make_unique<wxMenuBar>();
        auto file = std::make_unique<wxMenu>();
        file->Append(wxID_NEW, "&New\tCtrl+N");
        file->Append(wxID_OPEN, "&Open...\tCtrl+O");
        file->Append(wxID_SAVE, "&Save\tCtrl+S");
        file->Append(wxID_SAVEAS, "Save &as...\tCtrl+Shift+S");
        file->AppendSeparator();
        file->Append(wxID_EXIT, "E&xit\tCtrl+Q");
        menuBar->Append(file.get(), "&File");
        file.release(); // NOLINT(bugprone-unused-return-value,cert-err33-c)
        SetMenuBar(menuBar.release());
        CreateStatusBar();
        notebook_ = createControl<wxNotebook>(*this, wxID_ANY);
        buildGraphPage(*notebook_);
        buildComparisonPage(*notebook_);
        auto sizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
        sizer->Add(notebook_.get(), 1, wxEXPAND);
        SetSizer(sizer.release());
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                executeWithErrorHandling([this] {
                    if (confirmReplacement()) {
                        application_.clear();
                        currentPath_.clear();
                        refresh();
                    }
                });
            },
            wxID_NEW);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                executeWithErrorHandling([this] {
                    open();
                });
            },
            wxID_OPEN);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                executeWithErrorHandling([this] {
                    (void)save(false);
                });
            },
            wxID_SAVE);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                executeWithErrorHandling([this] {
                    (void)save(true);
                });
            },
            wxID_SAVEAS);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                Close();
            },
            wxID_EXIT);
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (busy_) {
                if (event.CanVeto()) {
                    closeRequested_ = true;
                    event.Veto();
                    SetStatusText("Closing after the current operation finishes...");
                } else {
                    worker_.join();
                    event.Skip();
                }
                return;
            }
            if (event.CanVeto() && application_.isModified()) {
                const auto answer = wxMessageBox("Save graph changes before closing?",
                                                 "DOR3",
                                                 wxYES_NO | wxCANCEL | wxICON_QUESTION,
                                                 this);
                if (answer == wxCANCEL) {
                    event.Veto();
                    return;
                }
                if (answer == wxYES) {
                    bool saved = false;
                    executeWithErrorHandling([&] {
                        saved = save(false);
                    });
                    if (!saved) {
                        event.Veto();
                        return;
                    }
                }
            }
            event.Skip();
        });
        refresh();
        Centre();
    }
};

class GraphColoringApp final : public wxApp {
    bool OnInit() override {
        try {
            auto frame = std::make_unique<MainFrame>();
            SetTopWindow(frame.get());
            frame->Show();
            frame.release(); // NOLINT(bugprone-unused-return-value,cert-err33-c)
            return true;
        } catch (const std::exception& error) {
            wxMessageBox(convertToWxString(error.what()), "DOR3 startup error", wxOK | wxICON_ERROR);
            return false;
        }
    }
};

wxIMPLEMENT_APP(GraphColoringApp);
