#include <application.hpp>
#include <charconv>
#include <cmath>
#include <format>
#include <functional>
#include <input_limits.hpp>
#include <memory>
#include <numbers>
#include <thread>
#include <wx/dcbuffer.h>
#include <wx/listctrl.h>
#include <wx/notebook.h>
#include <wx/spinctrl.h>
#include <wx/weakref.h>
#include <wx/wx.h>

wxString text(std::string_view value) {
    return wxString::FromUTF8(value.data(), value.size());
}

std::string utf8(const wxString& value) {
    return value.ToStdString(wxConvUTF8);
}

template <class T, std::size_t Size, class Name>
void fillChoice(wxChoice& choice, const T (&values)[Size], T selected, Name name) {
    choice.Clear();
    for (auto value : values) {
        const auto index = choice.Append(text(name(value)));
        if (value == selected) {
            choice.SetSelection(static_cast<int>(index));
        }
    }
}

template <class T, std::size_t Size>
Result<T> readChoice(const wxChoice& choice, const T (&values)[Size], std::string_view field) {
    const auto index = choice.GetSelection();
    if (index < 0 || static_cast<std::size_t>(index) >= Size) {
        return failure(ErrorCode::InvalidParameters, "Select a valid " + std::string(field) + ".");
    }
    return values[static_cast<std::size_t>(index)];
}

struct TableColumn {
    long index;
    std::string_view caption;
    int width;
};

// wxWidgets владеет дочерними окнами. Поля хранят слабые ссылки с отслеживанием их удаления
template <class T, class... Args>
wxWeakRef<T> control(wxWindow& parent, Args&&... args) {
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

void label(wxWindow& parent, wxSizer& sizer, const wxString& value) {
    auto item = control<wxStaticText>(parent, wxID_ANY, value);
    sizer.Add(item.get(), 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 8);
}

template <class T>
Result<T> parseNumber(wxString value, std::string_view field) {
    auto string = utf8(value.Trim().Trim(false));
    T parsed{};
    const auto result = std::from_chars(string.data(), string.data() + string.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != string.data() + string.size()) {
        return failure(ErrorCode::InvalidParameters, "Invalid " + std::string(field) + ".");
    }
    return parsed;
}

struct GenerationControls {
    wxWeakRef<wxSpinCtrl> count;
    wxWeakRef<wxChoice> topology;
    wxWeakRef<wxTextCtrl> probability, seed;

    void create(wxWindow& parent, wxSizer& sizer) {
        const GenerationParameters defaults;
        auto fields = std::make_unique<wxFlexGridSizer>(2, 6, 6);
        fields->AddGrowableCol(1);
        label(parent, *fields, "Vertices / max. size");
        count = control<wxSpinCtrl>(parent, wxID_ANY);
        count->SetRange(0, static_cast<int>(InputLimits::maxGeneratedVertices));
        count->SetValue(static_cast<int>(defaults.vertexCount));
        fields->Add(count.get(), 1, wxEXPAND);
        label(parent, *fields, "Topology");
        topology = control<wxChoice>(parent, wxID_ANY);
        fillChoice(*topology, supportedTopologies, defaults.topology, topologyName);
        fields->Add(topology.get(), 1, wxEXPAND);
        label(parent, *fields, "Edge probability");
        probability = control<wxTextCtrl>(
            parent, wxID_ANY, text(std::format("{}", defaults.probability)));
        fields->Add(probability.get(), 1, wxEXPAND);
        label(parent, *fields, "Seed");
        seed = control<wxTextCtrl>(parent, wxID_ANY, text(std::to_string(defaults.seed)));
        fields->Add(seed.get(), 1, wxEXPAND);
        addSizer(sizer, std::move(fields), 0, wxEXPAND | wxBOTTOM, 8);
    }

    Result<GenerationParameters> read() const {
        auto parsedProbability = parseNumber<double>(probability->GetValue(), "probability");
        auto parsedSeed = parseNumber<std::uint32_t>(seed->GetValue(), "seed");
        if (!parsedProbability) {
            return std::unexpected(parsedProbability.error());
        }
        if (!parsedSeed) {
            return std::unexpected(parsedSeed.error());
        }
        auto selectedTopology = readChoice(*topology, supportedTopologies, "topology");
        if (!selectedTopology) {
            return std::unexpected(selectedTopology.error());
        }
        return GenerationParameters{static_cast<std::size_t>(count->GetValue()),
                                    *selectedTopology,
                                    *parsedProbability,
                                    *parsedSeed};
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

    static wxColour color(std::size_t index) {
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
        DynamicArray<Position> positions;
        for (std::size_t i = 0; i < count; ++i) {
            const double angle =
                2 * std::numbers::pi * static_cast<double>(i) / static_cast<double>(count) -
                std::numbers::pi / 2;
            positions.push_back(
                {size.x / 2 + (count == 1 ? 0 : static_cast<int>(radius * std::cos(angle))),
                 size.y / 2 + (count == 1 ? 0 : static_cast<int>(radius * std::sin(angle)))});
        }
        dc.SetPen(wxPen(wxColour(175, 184, 198), 1));
        for (std::size_t i = 0; i < count; ++i) {
            const auto& neighbors = graph.neighbors(i)->get();
            for (std::size_t j = 0; j < neighbors.size(); ++j) {
                const auto target = neighbors[j];
                if (i < target) {
                    dc.DrawLine(
                        positions[i].x, positions[i].y, positions[target].x, positions[target].y);
                }
            }
        }
        for (std::size_t i = 0; i < count; ++i) {
            dc.SetBrush(wxBrush(application_.coloring() ? color(application_.coloring()->colors[i])
                                                        : wxColour(224, 232, 245)));
            dc.SetPen(wxPen(wxColour(75, 87, 110), 1));
            dc.DrawCircle(positions[i].x, positions[i].y, vertexRadius);
            const auto id = wxString::Format("%zu", i);
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
    DynamicArray<std::size_t> steps_;

    wxString OnGetItemText(long row, long column) const override {
        if (busy_) {
            return {};
        }
        const auto id = static_cast<std::size_t>(row);
        const auto& graph = application_.graph();
        if (!graph.containsVertex(id)) {
            return {};
        }
        const auto& neighbors = graph.neighbors(id)->get();
        switch (column) {
        case Column::Vertex:
            return wxString::Format("%zu", id);
        case Column::Label:
            return text(graph.vertex(id)->get().label);
        case Column::Degree:
            return wxString::Format("%zu", neighbors.size());
        case Column::Color:
            return application_.coloring()
                       ? wxString::Format("C%zu", application_.coloring()->colors[id])
                       : "—";
        case Column::Step:
            return application_.coloring() ? wxString::Format("%zu", steps_[id] + 1) : "—";
        case Column::Neighbors: {
            wxString list;
            for (std::size_t i = 0; i < neighbors.size(); ++i) {
                if (i) {
                    list += ", ";
                }
                list += wxString::Format("%zu", neighbors[i]);
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
            InsertColumn(column.index, text(column.caption), wxLIST_FORMAT_LEFT, column.width);
        }
        SetMinSize(wxSize(350, 180));
    }

    void update() {
        steps_.clear();
        const auto count = application_.graph().vertexCount();
        for (std::size_t i = 0; i < count; ++i) {
            steps_.push_back(0);
        }
        if (application_.coloring()) {
            const auto& order = application_.coloring()->order;
            for (std::size_t i = 0; i < count; ++i) {
                steps_[order[i]] = i;
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
    wxWeakRef<VertexTable> vertices_;
    wxWeakRef<wxStaticText> summary_;
    wxWeakRef<wxTextCtrl> vertexLabel_, edgeFirst_, edgeSecond_;
    wxWeakRef<wxChoice> algorithm_;
    wxWeakRef<wxSpinCtrl> repetitions_, points_;
    wxWeakRef<wxListCtrl> comparisons_;
    GenerationControls generation_, seriesGeneration_;
    std::jthread worker_;

    void showError(const Error& error) {
        wxMessageBox(text(formatError(error)), "DOR3", wxOK | wxICON_ERROR, this);
    }

    template <class Action>
    void protect(Action&& action) {
        try {
            action();
        } catch (const std::exception& error) {
            showError({ErrorCode::Unexpected, error.what()});
        }
    }

    template <class Action>
    void button(wxWindow& parent, wxSizer& sizer, const wxString& caption, Action action) {
        auto item = control<wxButton>(parent, wxID_ANY, caption);
        sizer.Add(item.get(), 0, wxEXPAND | wxBOTTOM, 6);
        item->Bind(wxEVT_BUTTON, [this, action](wxCommandEvent&) {
            protect(action);
        });
    }

    void refresh() {
        canvas_->Refresh();
        vertices_->update();
        wxString summary = wxString::Format("Vertices: %zu   Edges: %zu",
                                            application_.graph().vertexCount(),
                                            application_.graph().edgeCount());
        if (application_.coloring()) {
            summary +=
                wxString::Format("   Colors: %zu   Validated", application_.coloring()->colorCount);
        }
        summary_->SetLabel(summary);
        comparisons_->DeleteAllItems();
        const auto& rows = application_.measurements();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto& row = rows[i];
            const auto index = comparisons_->InsertItem(static_cast<long>(i), wxEmptyString);
            comparisons_->SetItem(index, ComparisonColumn::Vertices,
                                   wxString::Format("%zu", row.vertices));
            comparisons_->SetItem(index, ComparisonColumn::Edges,
                                   wxString::Format("%zu", row.edges));
            comparisons_->SetItem(index, ComparisonColumn::Algorithm,
                                   text(algorithmName(row.algorithm)));
            comparisons_->SetItem(index, ComparisonColumn::MedianTime,
                                   wxString::Format("%.3f", row.medianMicroseconds));
            comparisons_->SetItem(index, ComparisonColumn::Colors,
                                   wxString::Format("%zu", row.colors));
        }
        SetStatusText(currentPath_.empty() ? "Ready" : text(currentPath_.string()));
        SetTitle(application_.isModified() ? "DOR3 — Graph coloring *" : "DOR3 — Graph coloring");
        Layout();
    }

    void enableActions(bool enabled) {
        graphPanel_->Enable(enabled);
        comparisonPanel_->Enable(enabled);
        for (int id : {wxID_NEW, wxID_OPEN, wxID_SAVE, wxID_SAVEAS}) {
            GetMenuBar()->Enable(id, enabled);
        }
    }

    void setBusy(bool busy) {
        busy_ = busy;
        enableActions(!busy);
        canvas_->Refresh();
        vertices_->Refresh();
        SetStatusText(busy ? "Working..."
                           : currentPath_.empty() ? "Ready" : text(currentPath_.string()));
    }

    void runTask(std::function<Result<void>()> action,
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
                    result = failure(ErrorCode::Unexpected, error.what());
                }
                CallAfter([this, result = std::move(result), showComparison, onSuccess] {
                    protect([&] {
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
            path = utf8(dialog.GetPath());
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
        const auto path = std::filesystem::path(utf8(dialog.GetPath()));
        runTask(
            [this, path] {
                return application_.open(path);
            },
            false,
            [this, path] {
                currentPath_ = path;
            });
    }

    void changeEdge(bool remove) {
        auto first = parseNumber<VertexId>(edgeFirst_->GetValue(), "first vertex");
        auto second = parseNumber<VertexId>(edgeSecond_->GetValue(), "second vertex");
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
        graphPanel_ = control<wxPanel>(notebook, wxID_ANY);
        auto root = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        auto tools = std::make_unique<wxBoxSizer>(wxVERTICAL);
        label(*graphPanel_, *tools, "Manual editing");
        vertexLabel_ = control<wxTextCtrl>(*graphPanel_, wxID_ANY, "Vertex");
        tools->Add(vertexLabel_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        button(*graphPanel_, *tools, "Add vertex", [this] {
            auto result = application_.addVertex(utf8(vertexLabel_->GetValue()));
            if (!result) {
                showError(result.error());
            } else {
                refresh();
            }
        });
        auto edge = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        edgeFirst_ =
            control<wxTextCtrl>(*graphPanel_, wxID_ANY, "0", wxDefaultPosition, wxSize(90, -1));
        edgeSecond_ =
            control<wxTextCtrl>(*graphPanel_, wxID_ANY, "1", wxDefaultPosition, wxSize(90, -1));
        edge->Add(edgeFirst_.get(), 1, wxRIGHT, 6);
        edge->Add(edgeSecond_.get(), 1);
        addSizer(*tools, std::move(edge), 0, wxEXPAND | wxBOTTOM, 6);
        button(*graphPanel_, *tools, "Add edge", [this] {
            changeEdge(false);
        });
        button(*graphPanel_, *tools, "Remove edge", [this] {
            changeEdge(true);
        });
        tools->AddSpacer(10);
        label(*graphPanel_, *tools, "Generate graph");
        tools->AddSpacer(8);
        generation_.create(*graphPanel_, *tools);
        button(*graphPanel_, *tools, "Generate", [this] {
            auto parameters = generation_.read();
            if (!parameters) {
                showError(parameters.error());
                return;
            }
            if (!confirmReplacement()) {
                return;
            }
            runTask(
                [this, parameters = *parameters] {
                    return application_.generate(parameters);
                },
                false,
                [this] {
                    currentPath_.clear();
                });
        });
        tools->AddSpacer(10);
        label(*graphPanel_, *tools, "Coloring algorithm");
        tools->AddSpacer(8);
        algorithm_ = control<wxChoice>(*graphPanel_, wxID_ANY);
        fillChoice(*algorithm_, supportedColoringAlgorithms, ColoringAlgorithm::Sequential,
                   algorithmName);
        tools->Add(algorithm_.get(), 0, wxEXPAND | wxBOTTOM, 6);
        button(*graphPanel_, *tools, "Color graph", [this] {
            auto selectedAlgorithm = readChoice(*algorithm_, supportedColoringAlgorithms, "algorithm");
            if (!selectedAlgorithm) {
                showError(selectedAlgorithm.error());
                return;
            }
            runTask([this, algorithm = *selectedAlgorithm] {
                return application_.run(algorithm);
            });
        });
        button(*graphPanel_, *tools, "Compare both algorithms", [this] {
            const auto repetitions = static_cast<std::size_t>(repetitions_->GetValue());
            runTask(
                [this, repetitions] {
                    return application_.compare(repetitions);
                },
                true);
        });
        auto note = control<wxStaticText>(*graphPanel_,
                                          wxID_ANY,
                                          "Vertex IDs and colors start at 0.\nGreedy methods do "
                                          "not guarantee\nthe minimum number of colors.");
        tools->Add(note.get(), 0, wxTOP, 8);
        addSizer(*root, std::move(tools), 0, wxEXPAND | wxALL, 12);
        auto view = std::make_unique<wxBoxSizer>(wxVERTICAL);
        summary_ = control<wxStaticText>(*graphPanel_, wxID_ANY, "");
        view->Add(summary_.get(), 0, wxBOTTOM, 8);
        canvas_ = control<GraphCanvas>(*graphPanel_, application_, busy_);
        view->Add(canvas_.get(), 3, wxEXPAND | wxBOTTOM, 8);
        vertices_ = control<VertexTable>(*graphPanel_, application_, busy_);
        view->Add(vertices_.get(), 2, wxEXPAND);
        addSizer(*root, std::move(view), 1, wxEXPAND | wxALL, 12);
        graphPanel_->SetSizer(root.release());
        notebook.AddPage(graphPanel_.get(), "Graph");
    }

    void buildComparisonPage(wxNotebook& notebook) {
        comparisonPanel_ = control<wxPanel>(notebook, wxID_ANY);
        auto root = std::make_unique<wxBoxSizer>(wxHORIZONTAL);
        auto tools = std::make_unique<wxBoxSizer>(wxVERTICAL);
        label(*comparisonPanel_, *tools, "Comparison settings");
        tools->AddSpacer(8);
        label(*comparisonPanel_, *tools, "Repetitions (median)");
        repetitions_ = control<wxSpinCtrl>(*comparisonPanel_, wxID_ANY);
        repetitions_->SetRange(1, static_cast<int>(InputLimits::maxRepetitions));
        repetitions_->SetValue(static_cast<int>(BenchmarkDefaults::repetitions));
        tools->Add(repetitions_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        button(*comparisonPanel_, *tools, "Compare current graph", [this] {
            const auto repetitions = static_cast<std::size_t>(repetitions_->GetValue());
            runTask(
                [this, repetitions] {
                    return application_.compare(repetitions);
                },
                true);
        });
        tools->AddSpacer(12);
        label(*comparisonPanel_, *tools, "Series for plotting");
        tools->AddSpacer(8);
        seriesGeneration_.create(*comparisonPanel_, *tools);
        label(*comparisonPanel_, *tools, "Number of sizes");
        points_ = control<wxSpinCtrl>(*comparisonPanel_, wxID_ANY);
        points_->SetRange(1, static_cast<int>(InputLimits::maxSeriesPoints));
        points_->SetValue(static_cast<int>(BenchmarkDefaults::points));
        tools->Add(points_.get(), 0, wxEXPAND | wxTOP | wxBOTTOM, 6);
        button(*comparisonPanel_, *tools, "Compare size series", [this] {
            auto parameters = seriesGeneration_.read();
            if (!parameters) {
                showError(parameters.error());
                return;
            }
            const auto points = static_cast<std::size_t>(points_->GetValue());
            const auto repetitions = static_cast<std::size_t>(repetitions_->GetValue());
            runTask(
                [this, parameters = *parameters, points, repetitions] {
                    return application_.compareSeries(parameters, points, repetitions);
                },
                true);
        });
        button(*comparisonPanel_, *tools, "Export CSV...", [this] {
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
                exportBenchmark(application_.measurements(), utf8(dialog.GetPath()));
            if (!result) {
                showError(result.error());
            } else {
                SetStatusText("Comparison exported.");
            }
        });
        auto note =
            control<wxStaticText>(*comparisonPanel_,
                                  wxID_ANY,
                                  "Both methods use the same graph.\nIncludes ordering and "
                                  "allocation;\nvalidation runs outside the timer.\n"
                                  "All displayed results are validated.\n\nSeries keeps "
                                  "the current graph.\nRows remain until the next comparison.");
        tools->Add(note.get(), 0, wxTOP, 10);
        addSizer(*root, std::move(tools), 0, wxEXPAND | wxALL, 12);
        comparisons_ = control<wxListCtrl>(
            *comparisonPanel_, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLC_REPORT);
        constexpr TableColumn columns[] = {
            {ComparisonColumn::Vertices, "Vertices", 100},
            {ComparisonColumn::Edges, "Edges", 100},
            {ComparisonColumn::Algorithm, "Algorithm", 190},
            {ComparisonColumn::MedianTime, "Median (µs)", 100},
            {ComparisonColumn::Colors, "Colors", 100}};
        for (const auto& column : columns) {
            comparisons_->InsertColumn(column.index, text(column.caption), wxLIST_FORMAT_LEFT,
                                        column.width);
        }
        root->Add(comparisons_.get(), 1, wxEXPAND | wxALL, 12);
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
        notebook_ = control<wxNotebook>(*this, wxID_ANY);
        buildGraphPage(*notebook_);
        buildComparisonPage(*notebook_);
        auto sizer = std::make_unique<wxBoxSizer>(wxVERTICAL);
        sizer->Add(notebook_.get(), 1, wxEXPAND);
        SetSizer(sizer.release());
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                protect([this] {
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
                protect([this] {
                    open();
                });
            },
            wxID_OPEN);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                protect([this] {
                    (void)save(false);
                });
            },
            wxID_SAVE);
        Bind(
            wxEVT_MENU,
            [this](wxCommandEvent&) {
                protect([this] {
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
                    protect([&] {
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

class Application final : public wxApp {
    bool OnInit() override {
        try {
            auto frame = std::make_unique<MainFrame>();
            SetTopWindow(frame.get());
            frame->Show();
            frame.release(); // NOLINT(bugprone-unused-return-value,cert-err33-c)
            return true;
        } catch (const std::exception& error) {
            wxMessageBox(text(error.what()), "DOR3 startup error", wxOK | wxICON_ERROR);
            return false;
        }
    }
};

wxIMPLEMENT_APP(Application);
