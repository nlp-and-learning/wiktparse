// wiktbrowse: browser of JSONL files (wikt2jsonl output, one page per line).
// Left: the lines with their titles, a filter and "go to line". Right: the
// selected line as indented JSON in a read-only Kate view (highlighting,
// folding), and as a tree with readable labels of sections and senses.
//
// wiktbrowse [file.jsonl]
#include <QApplication>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/View>

#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

namespace {

// Offsets of the lines and their titles ("title" of a page, or "id")
struct JsonlIndex {
    std::vector<qint64> offsets;
    std::vector<QString> titles;

    void build(QFile &file) {
        offsets.clear();
        titles.clear();
        file.seek(0);
        while (!file.atEnd()) {
            offsets.push_back(file.pos());
            QByteArray line = file.readLine();
            QString title;
            for (const char *key : {"\"title\":\"", "\"id\":\""}) {
                if (int at = line.indexOf(key); at >= 0) {
                    at += static_cast<int>(strlen(key));
                    int end = line.indexOf('"', at);
                    title = QString::fromUtf8(line.mid(at, end - at));
                    break;
                }
            }
            titles.push_back(title);
        }
    }
};

QString scalar(const json &v) {
    if (v.is_string())
        return QString::fromStdString(v.get<std::string>());
    return QString::fromStdString(v.dump());
}

// A readable label of an array element: sections, senses, translations, languages
QString label(const json &v, size_t i) {
    QString text = QString("[%1]").arg(i);
    if (!v.is_object())
        return text + " " + scalar(v).left(120);
    auto add = [&](const char *key) {
        if (v.contains(key) && !v[key].is_null() && !(v[key].is_string() && v[key].get<std::string>().empty()))
            text += " " + scalar(v[key]).left(80);
    };
    if (v.contains("children")) { // a section of the tree
        add("section");
        if (v.contains("heading"))
            text += " — '" + scalar(v["heading"]).left(60) + "'";
        if (!v["senses"].empty())
            text += QString(" (%1 senses)").arg(v["senses"].size());
        if (!v["translations"].empty())
            text += QString(" (%1 translations)").arg(v["translations"].size());
    } else if (v.contains("language")) {
        add("code");
        add("language");
    } else if (v.contains("term")) {
        add("lang");
        add("term");
        add("sense");
    } else if (v.contains("id") && v.contains("text")) {
        add("id");
        add("text");
    }
    return text;
}

void addItems(QStandardItem *parent, const std::string &key, const json &v, const QString &name) {
    auto *keyItem = new QStandardItem(name);
    auto *valueItem = new QStandardItem(v.is_structured() ? QString() : scalar(v).left(500));
    keyItem->setEditable(false);
    valueItem->setEditable(false);
    if (!v.is_structured())
        valueItem->setToolTip(scalar(v).left(4000));
    parent->appendRow({keyItem, valueItem});
    if (v.is_object())
        for (const auto &[k, child] : v.items())
            addItems(keyItem, k, child, QString::fromStdString(k));
    else if (v.is_array()) {
        valueItem->setText(QString("%1 items").arg(v.size()));
        for (size_t i = 0; i < v.size(); ++i)
            addItems(keyItem, key, v[i], label(v[i], i));
    }
}

class Browser : public QMainWindow {
public:
    Browser() {
        auto *central = new QSplitter(this);
        auto *left = new QWidget;
        auto *leftLayout = new QVBoxLayout(left);
        filter_ = new QLineEdit;
        filter_->setPlaceholderText("filter titles");
        lineBox_ = new QSpinBox;
        lineBox_->setPrefix("line ");
        lineBox_->setMinimum(1);
        status_ = new QLabel;
        list_ = new QListWidget;
        auto *top = new QHBoxLayout;
        top->addWidget(filter_);
        top->addWidget(lineBox_);
        leftLayout->addLayout(top);
        leftLayout->addWidget(list_);
        leftLayout->addWidget(status_);

        tabs_ = new QTabWidget;
        auto *editor = KTextEditor::Editor::instance();
        document_ = editor->createDocument(this);
        document_->setHighlightingMode("JSON");
        view_ = document_->createView(tabs_);
        tree_ = new QTreeView;
        model_ = new QStandardItemModel(this);
        tree_->setModel(model_);
        tabs_->addTab(view_, "JSON");
        tabs_->addTab(tree_, "Tree");

        central->addWidget(left);
        central->addWidget(tabs_);
        central->setStretchFactor(1, 3);
        setCentralWidget(central);
        resize(1400, 900);

        connect(filter_, &QLineEdit::textChanged, this, [this] { fillList(); });
        connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
            if (auto *item = list_->item(row))
                showLine(item->data(Qt::UserRole).toInt());
        });
        connect(lineBox_, &QSpinBox::editingFinished, this, [this] { showLine(lineBox_->value() - 1); });
    }

    void open(const QString &path) {
        file_.close();
        file_.setFileName(path);
        if (!file_.open(QIODevice::ReadOnly)) {
            status_->setText("cannot open " + path);
            return;
        }
        index_.build(file_);
        lineBox_->setMaximum(std::max<int>(1, static_cast<int>(index_.offsets.size())));
        setWindowTitle(path + " — wiktbrowse");
        fillList();
        if (!index_.offsets.empty())
            list_->setCurrentRow(0);
    }

private:
    QFile file_;
    JsonlIndex index_;
    QLineEdit *filter_;
    QSpinBox *lineBox_;
    QLabel *status_;
    QListWidget *list_;
    QTabWidget *tabs_;
    KTextEditor::Document *document_;
    KTextEditor::View *view_;
    QTreeView *tree_;
    QStandardItemModel *model_;

    void fillList() {
        list_->clear();
        const QString text = filter_->text();
        for (size_t i = 0; i < index_.titles.size(); ++i) {
            if (!text.isEmpty() && !index_.titles[i].contains(text, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(QString("%1  %2").arg(i + 1).arg(index_.titles[i]));
            item->setData(Qt::UserRole, static_cast<int>(i));
            list_->addItem(item);
            if (list_->count() >= 100000)
                break;
        }
        status_->setText(QString("%1 lines, %2 shown").arg(index_.offsets.size()).arg(list_->count()));
    }

    void showLine(int line) {
        if (line < 0 || line >= static_cast<int>(index_.offsets.size()))
            return;
        file_.seek(index_.offsets[line]);
        const QByteArray raw = file_.readLine();
        json value = json::parse(raw.constData(), raw.constData() + raw.size(), nullptr, false);
        lineBox_->setValue(line + 1);
        document_->setReadWrite(true);
        document_->setText(value.is_discarded() ? QString::fromUtf8(raw)
                                                : QString::fromStdString(value.dump(2, ' ', false, json::error_handler_t::replace)));
        document_->setReadWrite(false);
        document_->setModified(false);
        model_->clear();
        model_->setHorizontalHeaderLabels({"Key", "Value"});
        if (!value.is_discarded())
            for (const auto &[k, child] : value.items())
                addItems(model_->invisibleRootItem(), k, child, QString::fromStdString(k));
        tree_->expandToDepth(2);
        tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    }
};

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Browser browser;
    QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                            : QFileDialog::getOpenFileName(nullptr, "JSONL file", {}, "JSONL (*.jsonl *.json);;All (*)");
    if (path.isEmpty())
        return 0;
    browser.open(path);
    browser.show();
    return app.exec();
}
