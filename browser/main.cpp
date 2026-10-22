// wiktbrowse: browser of JSONL files (wikt2jsonl output, one page per line; any
// JSONL, e.g. the Wikidata entities of wiktNeeded):
// plain .jsonl, or .jsonl.bz2 with its .jsonl.idx (only the block of the
// shown line is decompressed). Left: the lines with their titles, a filter and
// "go to line". Right: the selected line as indented JSON in a read-only
// qcodeedit view (Kate highlighting and folding), and as a tree with readable
// labels of sections and senses. F4 opens the line, indented, in Kate.
// View > Original: the page's wikitext (the <text> of its <page>, entities
// decoded) with Kate mediawiki highlighting, read from the dump at its "source"
// ([bz2 block, number in it]); the dump directory is set in Settings and kept
// in wiktbrowse.ini next to the binary.
//
// wiktbrowse [file.jsonl | file.jsonl.bz2]
#include <QApplication>
#include <QDir>
#include <QProcess>
#include <QShortcut>
#include <QFile>
#include <qxfiledialog.h>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenuBar>
#include <QSettings>
#include <QXmlStreamReader>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabWidget>
#include <QTreeView>
#include <QVBoxLayout>

#include <bzlib.h>
#include <qce/CodeEdit.h>
#include <qce/CodeEditArea.h>
#include <qce/RuleBasedFoldingProvider.h>
#include <qce/RulesHighlighter.h>
#include <qce/SimpleTextDocument.h>
#include <qce/kate/KatePaths.h>
#include <qce/kate/KateTheme.h>
#include <qce/kate/KateXmlReader.h>
#include <qce/margins/FoldingGutter.h>
#include <qce/margins/LineNumberGutter.h>

#include <memory>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

namespace {

// Lines of a JSONL file with their titles
class Lines {
public:
    virtual ~Lines() = default;
    [[nodiscard]] virtual size_t count() const = 0;
    [[nodiscard]] virtual QString title(size_t i) const = 0;
    [[nodiscard]] virtual QByteArray line(size_t i) = 0;
};

// "title" (wikt2jsonl pages), or "id" with the English label (Wikidata entities: Q1084 noun)
QString titleOf(const QByteArray &line) {
    auto value = [&](const char *key) -> QString {
        int at = line.indexOf(key);
        if (at < 0)
            return {};
        at += static_cast<int>(strlen(key));
        return QString::fromUtf8(line.mid(at, line.indexOf('"', at) - at));
    };
    if (auto title = value("\"title\":\""); !title.isEmpty())
        return title;
    QString id = value("\"id\":\"");
    if (auto english = value("\"en\":{\"language\":\"en\",\"value\":\""); !english.isEmpty())
        id += "  " + english;
    return id;
}

class PlainLines : public Lines {
public:
    explicit PlainLines(const QString &path) : file_(path) {
        if (!file_.open(QIODevice::ReadOnly))
            return;
        while (!file_.atEnd()) {
            offsets_.push_back(file_.pos());
            titles_.push_back(titleOf(file_.readLine()));
        }
    }
    [[nodiscard]] size_t count() const override { return offsets_.size(); }
    [[nodiscard]] QString title(size_t i) const override { return titles_[i]; }
    QByteArray line(size_t i) override {
        file_.seek(offsets_[i]);
        return file_.readLine();
    }

private:
    QFile file_;
    std::vector<qint64> offsets_;
    std::vector<QString> titles_;
};

// A .jsonl.bz2 of independent bz2 blocks with its index (tab-separated): per
// block "offset count uncompressed", then "\tstart\ttitle" per entry (start in
// the uncompressed block), at the end EOF=<size>. An entry ends where the next
// one starts, the last at uncompressed; a block where the next one starts, the
// last at EOF.
class Bz2Lines : public Lines {
public:
    Bz2Lines(const QString &path, const QString &indexPath) : file_(path) {
        file_.open(QIODevice::ReadOnly);
        QFile index(indexPath);
        if (!index.open(QIODevice::ReadOnly))
            return;
        quint64 end = 0;
        while (!index.atEnd()) {
            QByteArray l = index.readLine();
            l.chop(l.endsWith('\n') ? 1 : 0);
            if (l.startsWith('#'))
                continue;
            if (l.startsWith("EOF=")) {
                end = l.mid(4).toULongLong();
                continue;
            }
            const auto parts = l.split('\t');
            if (l.startsWith('\t')) {
                if (parts.size() >= 3) {
                    starts_.push_back(parts[1].toUInt());
                    titles_.push_back(QString::fromUtf8(l.mid(parts[1].size() + 2)));
                }
            } else if (parts.size() == 3)
                blocks_.push_back({parts[0].toULongLong(), 0, parts[2].toUInt(), titles_.size()});
        }
        if (!end)
            end = static_cast<quint64>(file_.size());
        for (size_t b = 0; b < blocks_.size(); ++b)
            blocks_[b].compressed = static_cast<unsigned>((b + 1 < blocks_.size() ? blocks_[b + 1].offset : end) - blocks_[b].offset);
    }
    [[nodiscard]] size_t count() const override { return titles_.size(); }
    [[nodiscard]] QString title(size_t i) const override { return titles_[i]; }
    QByteArray line(size_t i) override {
        auto it = std::upper_bound(blocks_.begin(), blocks_.end(), i,
                                   [](size_t line, const Block &b) { return line < b.firstLine; });
        if (it == blocks_.begin() || i >= titles_.size())
            return {};
        const Block &b = *std::prev(it);
        if (cached_ != b.offset) {
            file_.seek(static_cast<qint64>(b.offset));
            QByteArray compressed = file_.read(b.compressed);
            text_ = QByteArray(static_cast<qsizetype>(b.uncompressed), '\0');
            unsigned length = b.uncompressed;
            if (BZ2_bzBuffToBuffDecompress(text_.data(), &length, compressed.data(), b.compressed, 0, 0) != BZ_OK) {
                cached_ = ~0ull;
                return {};
            }
            text_.truncate(length);
            cached_ = b.offset;
        }
        const bool last = it == blocks_.end() ? i + 1 == titles_.size() : i + 1 == it->firstLine;
        const qsizetype from = starts_[i], to = last ? text_.size() : starts_[i + 1];
        QByteArray entry = text_.mid(from, to - from);
        entry.chop(entry.endsWith('\n') ? 1 : 0);
        return entry;
    }

private:
    struct Block {
        quint64 offset;
        unsigned compressed, uncompressed;
        size_t firstLine;
    };
    QFile file_;
    std::vector<Block> blocks_;
    std::vector<QString> titles_;
    std::vector<unsigned> starts_; // of the entries in their uncompressed block
    QByteArray text_;              // the decompressed block at cached_
    quint64 cached_ = ~0ull;
};

QString scalar(const json &v) {
    return v.is_string() ? QString::fromStdString(v.get<std::string>()) : QString::fromStdString(v.dump());
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
    // Empty fields are left out of the JSONL, so every key is optional
    if (v.contains("section")) {
        add("section");
        if (v.contains("heading"))
            text += " — '" + scalar(v["heading"]).left(60) + "'";
        if (v.contains("senses"))
            text += QString(" (%1 senses)").arg(v["senses"].size());
        if (v.contains("translations"))
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

void addItems(QStandardItem *parent, const json &v, const QString &name) {
    auto *keyItem = new QStandardItem(name);
    auto *valueItem = new QStandardItem(v.is_structured() ? QString() : scalar(v).left(500));
    keyItem->setEditable(false);
    valueItem->setEditable(false);
    if (!v.is_structured())
        valueItem->setToolTip(scalar(v).left(4000));
    parent->appendRow({keyItem, valueItem});
    if (v.is_object())
        for (const auto &[k, child] : v.items())
            addItems(keyItem, child, QString::fromStdString(k));
    else if (v.is_array()) {
        valueItem->setText(QString("%1 items").arg(v.size()));
        for (size_t i = 0; i < v.size(); ++i)
            addItems(keyItem, v[i], label(v[i], i));
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
        setupEditor(json_, "json.xml");
        setupEditor(wiki_, "mediawiki.xml");
        tree_ = new QTreeView;
        model_ = new QStandardItemModel(this);
        tree_->setModel(model_);
        tabs_->addTab(json_.edit, "JSON");
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
        // F4: the line, indented, in Kate itself (a read-only file in the temporary directory)
        connect(new QShortcut(QKeySequence(Qt::Key_F4), this), &QShortcut::activated, this, [this] { openInKate(); });

        auto *file = menuBar()->addMenu("&File");
        file->addAction("&Open...", QKeySequence::Open, this, [this] {
            if (auto path = choose(this, QFileInfo(path_).absolutePath()); !path.isEmpty())
                open(path);
        });
        file->addSeparator();
        file->addAction("&Quit", QKeySequence::Quit, this, &QWidget::close);

        auto *view = menuBar()->addMenu("&View");
        originalAction_ = view->addAction("&Original (wikitext from the dump)");
        originalAction_->setCheckable(true);
        originalAction_->setChecked(settings().value("view/original", false).toBool());
        connect(originalAction_, &QAction::toggled, this, [this](bool on) {
            settings().setValue("view/original", on);
            showOriginalTab(on);
        });
        showOriginalTab(originalAction_->isChecked());
        auto *setup = menuBar()->addMenu("&Settings");
        setup->addAction("&Dump directory...", this, [this] {
            auto dir = QxFileDialog::getExistingDirectory(this, "Directory of the *-pages-articles-multistream.xml.bz2 dumps",
                                                          dumpDir());
            if (!dir.isEmpty()) {
                settings().setValue("dumps/dir", dir);
                showOriginal();
            }
        });
    }

    // A JSONL file chosen in the dialog, empty when cancelled
    static QString choose(QWidget *parent, const QString &dir = {}) {
        return QxFileDialog::getOpenFileName(parent, "JSONL file", dir, "JSONL (*.jsonl *.jsonl.bz2 *.json);;All (*)");
    }

    void open(const QString &path) {
        QString index = path;
        if (path.endsWith(".bz2"))
            index = path.left(path.size() - 4) + ".idx";
        if (path.endsWith(".bz2") && QFileInfo::exists(index))
            lines_ = std::make_unique<Bz2Lines>(path, index);
        else
            lines_ = std::make_unique<PlainLines>(path);
        path_ = path;
        json_.doc->setText({});
        wiki_.doc->setText({});
        model_->clear();
        lineBox_->setMaximum(std::max<int>(1, static_cast<int>(lines_->count())));
        setWindowTitle(path + " — wiktbrowse");
        fillList();
        if (lines_->count() > 0)
            list_->setCurrentRow(0);
    }

private:
    // wiktbrowse.ini next to the binary
    static QSettings &settings() {
        static QSettings s(QCoreApplication::applicationDirPath() + "/wiktbrowse.ini", QSettings::IniFormat);
        return s;
    }

    static QString dumpDir() { return settings().value("dumps/dir").toString(); }

    void showOriginalTab(bool on) {
        const int at = tabs_->indexOf(wiki_.edit);
        if (on && at < 0)
            tabs_->addTab(wiki_.edit, "Original");
        else if (!on && at >= 0)
            tabs_->removeTab(at);
        if (on)
            showOriginal();
    }

    // The newest dump of a wiki in the dump directory: <wiki>wiktionary-<date>-pages-articles-multistream.xml.bz2
    static QString dumpOf(const QString &wiki) {
        QDir dir(dumpDir());
        auto files = dir.entryList({wiki + "wiktionary-*-pages-articles-multistream.xml.bz2"}, QDir::Files, QDir::Name);
        return files.isEmpty() ? QString() : dir.filePath(files.last());
    }

    // The wikitext of page number index of the bz2 stream at offset: a multistream
    // dump ends each stream (of about 100 pages), so it is read to the stream end
    static QString pageText(const QString &path, qint64 offset, qint64 index, QString &error) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || !file.seek(offset)) {
            error = "cannot read " + path;
            return {};
        }
        bz_stream stream{};
        if (BZ2_bzDecompressInit(&stream, 0, 0) != BZ_OK) {
            error = "bzip2 init failed";
            return {};
        }
        QByteArray xml = "<pages>", in, out(1 << 20, '\0');
        int status = BZ_OK;
        while (status == BZ_OK) {
            if (stream.avail_in == 0) {
                in = file.read(1 << 16);
                if (in.isEmpty())
                    break;
                stream.next_in = in.data();
                stream.avail_in = static_cast<unsigned>(in.size());
            }
            stream.next_out = out.data();
            stream.avail_out = static_cast<unsigned>(out.size());
            status = BZ2_bzDecompress(&stream);
            xml.append(out.constData(), out.size() - stream.avail_out);
        }
        BZ2_bzDecompressEnd(&stream);
        if (status != BZ_STREAM_END) {
            error = QString("no bz2 stream at %1 of %2").arg(offset).arg(path);
            return {};
        }
        xml += "</pages>";
        QXmlStreamReader reader(xml);
        qint64 page = -1;
        while (!reader.atEnd()) {
            if (reader.readNext() != QXmlStreamReader::StartElement)
                continue;
            if (reader.name() == u"page")
                ++page;
            else if (reader.name() == u"text" && page == index)
                return reader.readElementText();
        }
        error = QString("no page %1 in the stream at %2").arg(index).arg(offset);
        return {};
    }

    void showOriginal() {
        if (tabs_->indexOf(wiki_.edit) < 0)
            return;
        QString text;
        if (!current_.is_object() || !current_.contains("wiki") || !current_.contains("source") ||
            !current_["source"].is_array() || current_["source"].size() != 2)
            text = "(no \"wiki\" and \"source\" in this line)";
        else if (dumpDir().isEmpty())
            text = "(no dump directory: Settings > Dump directory)";
        else {
            const auto wiki = QString::fromStdString(current_["wiki"].get<std::string>());
            const auto path = dumpOf(wiki);
            QString error;
            if (path.isEmpty())
                error = "no dump of " + wiki + " in " + dumpDir();
            else
                text = pageText(path, current_["source"][0].get<qint64>(), current_["source"][1].get<qint64>(), error);
            if (!error.isEmpty())
                text = "(" + error + ")";
        }
        wiki_.doc->setText(text);
    }

    QAction *originalAction_ = nullptr;
    json current_;
    std::unique_ptr<Lines> lines_;
    QString path_;
    QLineEdit *filter_;
    QSpinBox *lineBox_;
    QLabel *status_;
    QListWidget *list_;
    QTabWidget *tabs_;
    // A read-only qcodeedit view with Kate highlighting and folding
    struct CodeView {
        qce::SimpleTextDocument *doc = nullptr;
        qce::CodeEdit *edit = nullptr;
        std::unique_ptr<qce::LineNumberGutter> lineNumbers;
        std::unique_ptr<qce::FoldingGutter> foldGutter;
        std::unique_ptr<qce::RulesHighlighter> highlighter;
        std::unique_ptr<qce::RuleBasedFoldingProvider> foldProvider;
    };
    CodeView json_; // the line as indented JSON
    CodeView wiki_; // the page's wikitext from the dump
    QTreeView *tree_;
    QStandardItemModel *model_;

    // Read-only qcodeedit with Kate's JSON syntax (downloaded Kate data) and folding
    void setupEditor(CodeView &view, const QString &syntaxFile) {
        view.doc = new qce::SimpleTextDocument(this);
        view.edit = new qce::CodeEdit;
        view.edit->setDocument(view.doc);
        view.edit->area()->setReadOnly(true);
        view.lineNumbers = std::make_unique<qce::LineNumberGutter>(view.doc);
        view.lineNumbers->setFont(view.edit->area()->font());
        view.edit->addLeftMargin(view.lineNumbers.get());
        view.foldGutter = std::make_unique<qce::FoldingGutter>(&view.edit->area()->foldState(),
                                                           [edit = view.edit](int line) { edit->area()->toggleFoldAt(line); });
        view.edit->addLeftMargin(view.foldGutter.get());

        const auto theme = KateTheme::loadDefault();
        QPalette palette = view.edit->area()->palette();
        if (theme.editorBackground.isValid()) {
            palette.setColor(QPalette::Base, theme.editorBackground);
            palette.setColor(QPalette::Window, theme.editorBackground);
        }
        if (auto normal = theme.styles.constFind("Normal"); normal != theme.styles.constEnd() && normal->fg.isValid())
            palette.setColor(QPalette::Text, normal->fg);
        view.edit->setPalette(palette);
        view.edit->area()->setPalette(palette);
        view.edit->area()->viewport()->setPalette(palette);
        view.edit->setGutterColors({theme.iconBorder, theme.lineNumbers, theme.separator});

        const QString syntax = qce::kate::syntaxDir() + "/" + syntaxFile;
        view.highlighter = theme.isValid() ? KateXmlReader::load(syntax, theme) : KateXmlReader::load(syntax);
        if (view.highlighter) {
            view.edit->area()->setHighlighter(view.highlighter.get());
            view.foldProvider = std::make_unique<qce::RuleBasedFoldingProvider>(view.highlighter.get());
            for (const char *region : {"Brace1", "brace", "curly"})
                view.foldProvider->setPlaceholderFor(region, "{…}");
            for (const char *region : {"Bracket1", "bracket", "square"})
                view.foldProvider->setPlaceholderFor(region, "[…]");
            view.edit->area()->setFoldingProvider(view.foldProvider.get());
        }
        view.edit->area()->setWordWrap(true);
    }

    void fillList() {
        list_->clear();
        if (!lines_)
            return;
        const QString text = filter_->text();
        for (size_t i = 0; i < lines_->count(); ++i) {
            if (!text.isEmpty() && !lines_->title(i).contains(text, Qt::CaseInsensitive))
                continue;
            auto *item = new QListWidgetItem(QString("%1  %2").arg(i + 1).arg(lines_->title(i)));
            item->setData(Qt::UserRole, static_cast<int>(i));
            list_->addItem(item);
            if (list_->count() >= 200000)
                break;
        }
        status_->setText(QString("%1 lines, %2 shown").arg(lines_->count()).arg(list_->count()));
    }

    void openInKate() {
        const QString dir = QDir::tempPath() + "/wiktbrowse";
        QDir().mkpath(dir);
        QString name = lines_ ? lines_->title(static_cast<size_t>(lineBox_->value() - 1)) : QString();
        name.replace('/', '_');
        const QString path = QString("%1/%2 %3.json").arg(dir).arg(lineBox_->value()).arg(name.left(80));
        QFile file(path);
        file.setPermissions(QFileDevice::WriteOwner | QFileDevice::ReadOwner);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return;
        file.write(json_.doc->toPlainText().toUtf8());
        file.close();
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        QProcess::startDetached("kate", {path});
    }

    void showLine(int line) {
        if (!lines_ || line < 0 || line >= static_cast<int>(lines_->count()))
            return;
        const QByteArray raw = lines_->line(static_cast<size_t>(line));
        json value = json::parse(raw.constData(), raw.constData() + raw.size(), nullptr, false);
        lineBox_->setValue(line + 1);
        json_.doc->setText(value.is_discarded()
                              ? QString::fromUtf8(raw)
                              : QString::fromStdString(value.dump(2, ' ', false, json::error_handler_t::replace)));
        model_->clear();
        model_->setHorizontalHeaderLabels({"Key", "Value"});
        if (!value.is_discarded())
            for (const auto &[k, child] : value.items())
                addItems(model_->invisibleRootItem(), child, QString::fromStdString(k));
        tree_->expandToDepth(2);
        tree_->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        current_ = value.is_discarded() ? json() : std::move(value);
        showOriginal();
    }
};

} // namespace

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    Browser browser;
    QString path = argc > 1 ? QString::fromLocal8Bit(argv[1])
                            : Browser::choose(nullptr);
    if (path.isEmpty())
        return 0;
    browser.open(path);
    browser.show();
    return app.exec();
}
