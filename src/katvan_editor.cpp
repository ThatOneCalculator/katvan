/*
 * This file is part of Katvan
 * Copyright (c) 2024 Igor Khanin
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include "katvan_editor.h"
#include "katvan_highlighter.h"
#include "katvan_spellchecker.h"

#include <QAbstractTextDocumentLayout>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QShortcut>
#include <QTextBlock>
#include <QTimer>

namespace katvan {

static constexpr QChar LRM_MARK = (ushort)0x200e;
static constexpr QChar RLM_MARK = (ushort)0x200f;
static constexpr QChar LRE_MARK = (ushort)0x202a;
static constexpr QChar RLE_MARK = (ushort)0x202b;
static constexpr QChar PDF_MARK = (ushort)0x202c;
static constexpr QChar LRO_MARK = (ushort)0x202d;
static constexpr QChar RLO_MARK = (ushort)0x202e;
static constexpr QChar LRI_MARK = (ushort)0x2066;
static constexpr QChar RLI_MARK = (ushort)0x2067;
static constexpr QChar PDI_MARK = (ushort)0x2069;

static constexpr QKeyCombination TEXT_DIRECTION_TOGGLE(Qt::CTRL | Qt::SHIFT | Qt::Key_X);
static constexpr QKeyCombination INSERT_POPUP(Qt::CTRL | Qt::SHIFT | Qt::Key_I);

class LineNumberGutter : public QWidget
{
public:
    LineNumberGutter(Editor *editor) : QWidget(editor), d_editor(editor) {}

    QSize sizeHint() const override
    {
        return QSize(d_editor->lineNumberGutterWidth(), 0);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        d_editor->lineNumberGutterPaintEvent(this, event);
    }

private:
    Editor *d_editor;
};

Editor::Editor(QWidget* parent)
    : QTextEdit(parent)
    , d_pendingSUggestionsPosition(-1)
{
    setAcceptRichText(false);

    d_spellChecker = new SpellChecker(this);
    connect(d_spellChecker, &SpellChecker::suggestionsReady, this, &Editor::spellingSuggestionsReady);

    d_highlighter = new Highlighter(document(), d_spellChecker);

    d_leftLineNumberGutter = new LineNumberGutter(this);
    // d_rightLineNumberGutter = new LineNumberGutter(this);

    connect(document(), &QTextDocument::blockCountChanged, this, &Editor::updateLineNumberGutterWidth);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &Editor::updateLineNumberGutters);
    connect(this, &QTextEdit::textChanged, this, &Editor::updateLineNumberGutters);
    connect(this, &QTextEdit::cursorPositionChanged, this, &Editor::updateLineNumberGutters);

    updateLineNumberGutters();

    QShortcut* toggleDirection = new QShortcut(this);
    toggleDirection->setKey(TEXT_DIRECTION_TOGGLE);
    toggleDirection->setContext(Qt::WidgetShortcut);
    connect(toggleDirection, &QShortcut::activated, this, &Editor::toggleTextBlockDirection);

    QShortcut* insertPopup = new QShortcut(this);
    insertPopup->setKey(INSERT_POPUP);
    insertPopup->setContext(Qt::WidgetShortcut);
    connect(insertPopup, &QShortcut::activated, this, &Editor::popupInsertMenu);

    d_debounceTimer = new QTimer(this);
    d_debounceTimer->setSingleShot(true);
    d_debounceTimer->setInterval(500);
    d_debounceTimer->callOnTimeout(this, [this]() {
        Q_EMIT contentModified(toPlainText());
    });

    connect(this, &QTextEdit::textChanged, [this]() {
        d_debounceTimer->start();
    });
}

QMenu* Editor::createInsertMenu()
{
    QMenu* menu = new QMenu();

    menu->addAction(tr("Right-to-Left Mark"), this, [this]() { insertMark(RLM_MARK); });
    menu->addAction(tr("Left-to-Right Mark"), this, [this]() { insertMark(LRM_MARK); });

    menu->addSeparator();

    menu->addAction(tr("Right-to-Left Isolate"), this, [this]() { insertSurroundingMarks(RLI_MARK, PDI_MARK); });
    menu->addAction(tr("Left-to-Right Isolate"), this, [this]() { insertSurroundingMarks(LRI_MARK, PDI_MARK); });
    menu->addAction(tr("Right-to-Left Embedding"), this, [this]() { insertSurroundingMarks(RLE_MARK, PDF_MARK); });
    menu->addAction(tr("Left-to-Right Embedding"), this, [this]() { insertSurroundingMarks(LRE_MARK, PDF_MARK); });
    menu->addAction(tr("Right-to-Left Override"), this, [this]() { insertSurroundingMarks(RLO_MARK, PDF_MARK); });
    menu->addAction(tr("Left-to-Right Override"), this, [this]() { insertSurroundingMarks(LRO_MARK, PDF_MARK); });

    menu->addSeparator();

    QAction* insertInlineMathAction = menu->addAction(tr("Inline &Math"), this, [this]() {
        insertSurroundingMarks(LRI_MARK + QStringLiteral("$"), QStringLiteral("$") + PDI_MARK);
    });
    insertInlineMathAction->setShortcut(Qt::CTRL | Qt::Key_M);

    return menu;
}

void Editor::toggleTextBlockDirection()
{
    Qt::LayoutDirection currentDirection = textCursor().block().textDirection();
    if (currentDirection == Qt::LeftToRight) {
        setTextBlockDirection(Qt::RightToLeft);
    }
    else {
        setTextBlockDirection(Qt::LeftToRight);
    }
}

void Editor::setTextBlockDirection(Qt::LayoutDirection dir)
{
    QTextCursor cursor = textCursor();

    QTextBlockFormat fmt;
    fmt.setLayoutDirection(dir);
    cursor.mergeBlockFormat(fmt);
}

void Editor::goToBlock(int blockNum)
{
    QTextBlock block = document()->findBlockByNumber(blockNum);
    if (block.isValid()) {
        setTextCursor(QTextCursor(block));
    }
}

void Editor::forceRehighlighting()
{
    QTimer::singleShot(0, d_highlighter, &QSyntaxHighlighter::rehighlight);
}

bool Editor::event(QEvent* event)
{
#ifdef Q_OS_LINUX
    if (event->type() == QEvent::ShortcutOverride) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)) {
            if (keyEvent->nativeScanCode() == 50) {
                d_pendingDirectionChange = Qt::LeftToRight;
            }
            else if (keyEvent->nativeScanCode() == 62) {
                d_pendingDirectionChange = Qt::RightToLeft;
            }
            else {
                d_pendingDirectionChange.reset();
            }
        }
    }
#endif
    return QTextEdit::event(event);
}

void Editor::contextMenuEvent(QContextMenuEvent* event)
{
    QTextCursor cursor = cursorForPosition(event->pos());
    QString misspelledWord = misspelledWordAtCursor(cursor);

    d_contextMenu = createStandardContextMenu(event->pos());
    d_contextMenu->setAttribute(Qt::WA_DeleteOnClose);

    if (!misspelledWord.isEmpty()) {
        QAction* origFirstAction = d_contextMenu->actions().first();

        QAction* placeholderAction = new QAction(tr("Calculating Suggestions..."));
        placeholderAction->setEnabled(false);

        QAction* addToPersonalAction = new QAction(tr("Add to Personal Dictionary"));
        connect(addToPersonalAction, &QAction::triggered, this, [this, misspelledWord, cursor]() {
            d_spellChecker->addToPersonalDictionary(misspelledWord);
            d_highlighter->rehighlightBlock(cursor.block());
        });

        d_contextMenu->insertAction(origFirstAction, placeholderAction);
        d_contextMenu->insertAction(origFirstAction, addToPersonalAction);
        d_contextMenu->insertSeparator(origFirstAction);
    }

    d_contextMenu->addSeparator();
    d_contextMenu->addAction(tr("Toggle Text Direction"), TEXT_DIRECTION_TOGGLE, this, &Editor::toggleTextBlockDirection);

    if (!misspelledWord.isEmpty()) {
        // Request the suggestions after menu was created, but before it is
        // shown. If suggestions are already in cache, the suggestionsReady
        // signal will be instantly invoked as a direct connection.
        d_pendingSuggestionsWord = misspelledWord;
        d_pendingSUggestionsPosition = cursor.position();
        d_spellChecker->requestSuggestions(misspelledWord, cursor.position());
    }
    d_contextMenu->popup(event->globalPos());
}

void Editor::keyPressEvent(QKeyEvent* event)
{
    if (event->modifiers() == Qt::ShiftModifier && event->key() == Qt::Key_Return) {
        // For displayed line numbers to make sense, each QTextBlock must correspond
        // to one plain text line - meaning no newlines allowed in the middle of a
        // block. Since we only ever import and export plain text to the editor, the
        // only way to create such a newline is by typing it with Shift+Return; disable
        // this by sending the base implementation an event without the Shift modifier.
        QKeyEvent overrideEvent(
            QEvent::KeyPress,
            event->key(),
            Qt::NoModifier,
            QLatin1String("\n"),
            event->isAutoRepeat());

        QTextEdit::keyPressEvent(&overrideEvent);
        return;
    }
    QTextEdit::keyPressEvent(event);
}

void Editor::keyReleaseEvent(QKeyEvent* event)
{
    if (d_pendingDirectionChange) {
        setTextBlockDirection(d_pendingDirectionChange.value());
        d_pendingDirectionChange.reset();
        return;
    }
    QTextEdit::keyReleaseEvent(event);
}

void Editor::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);

    QRect cr = contentsRect();
    int gutterWidth = lineNumberGutterWidth();
    int verticalScrollBarWidth = verticalScrollBar()->isVisible() ? verticalScrollBar()->width() : 0;

    if (layoutDirection() == Qt::LeftToRight) {
        d_leftLineNumberGutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth, cr.height()));
        // d_rightLineNumberGutter->setGeometry(QRect(cr.right() - gutterWidth - verticalScrollBarWidth, cr.top(), gutterWidth, cr.height()));
    }
    else {
        // d_rightLineNumberGutter->setGeometry(QRect(cr.left() + verticalScrollBarWidth, cr.top(), gutterWidth, cr.height()));
        d_leftLineNumberGutter->setGeometry(QRect(cr.right() - gutterWidth, cr.top(), gutterWidth, cr.height()));
    }
}

void Editor::popupInsertMenu()
{
    QMenu* insertMenu = createInsertMenu();
    insertMenu->setAttribute(Qt::WA_DeleteOnClose);

    QPoint globalPos = viewport()->mapToGlobal(cursorRect().topLeft());
    insertMenu->exec(globalPos);
}

QString Editor::misspelledWordAtCursor(QTextCursor cursor)
{
    if (cursor.isNull()) {
        return QString();
    }
    size_t pos = cursor.positionInBlock();

    HighlighterStateBlockData* blockData = dynamic_cast<HighlighterStateBlockData*>(cursor.block().userData());
    if (!blockData) {
        return QString();
    }

    const auto& words = blockData->misspelledWords();
    for (const auto& w : words) {
        if (pos >= w.startPos && pos <= w.startPos + w.length) {
            return cursor.block().text().sliced(w.startPos, w.length);
        }
    }
    return QString();
}

void Editor::spellingSuggestionsReady(const QString& word, int position, const QStringList& suggestions)
{
    if (!d_contextMenu) {
        return;
    }

    if (d_pendingSuggestionsWord != word || d_pendingSUggestionsPosition != position) {
        return;
    }
    d_pendingSuggestionsWord.clear();
    d_pendingSUggestionsPosition = -1;

    QAction* suggestionsPlaceholder = d_contextMenu->actions().first();
    if (suggestions.isEmpty()) {
        suggestionsPlaceholder->setText(tr("No Suggestions Available"));
    }
    else {
        QMenu* suggestionsMenu = new QMenu(tr("%n Suggestion(s)", "", suggestions.size()));
        for (const QString& suggestion : suggestions) {
            suggestionsMenu->addAction(QString(suggestion), [this, position, suggestion]() {
                changeWordAtPosition(position, suggestion);
            });
        }

        d_contextMenu->insertMenu(suggestionsPlaceholder, suggestionsMenu);
        d_contextMenu->removeAction(suggestionsPlaceholder);
    }
}

void Editor::changeWordAtPosition(int position, const QString& into)
{
    QTextBlock block = document()->findBlock(position);
    if (!block.isValid()) {
        return;
    }

    QTextCursor cursor{ block };
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, position - block.position());
    cursor.select(QTextCursor::WordUnderCursor);
    cursor.insertText(into);
}

void Editor::insertMark(QChar mark)
{
    textCursor().insertText(mark);
}

void Editor::insertSurroundingMarks(QString before, QString after)
{
    QTextCursor cursor = textCursor();
    cursor.insertText(before + cursor.selectedText() + after);
    cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, after.length());
    setTextCursor(cursor);
}

int Editor::lineNumberGutterWidth()
{
    int digits = 1;
    int max = qMax(1, document()->blockCount());
    while (max >= 10) {
        max /= 10;
        digits++;
    }

    int space = 10 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
    return space;
}

void Editor::updateLineNumberGutterWidth()
{
    int gutterWidth = lineNumberGutterWidth();
    setViewportMargins(gutterWidth, 0, gutterWidth, 0);
}

void Editor::updateLineNumberGutters()
{
    QRect cr = contentsRect();
    d_leftLineNumberGutter->update(0, cr.y(), d_leftLineNumberGutter->width(), cr.height());
    // d_rightLineNumberGutter->update(0, cr.y(), d_rightLineNumberGutter->width(), cr.height());

    updateLineNumberGutterWidth();

    int dy = verticalScrollBar()->sliderPosition();
    if (dy >= 0) {
        d_leftLineNumberGutter->scroll(0, dy);
        // d_rightLineNumberGutter->scroll(0, dy);
    }
}

QTextBlock Editor::getFirstVisibleBlock()
{
    QTextDocument* doc = document();
    QRect viewportGeometry = viewport()->geometry();

    for (QTextBlock it = doc->firstBlock(); it.isValid(); it = it.next()) {
        QRectF blockRect = doc->documentLayout()->blockBoundingRect(it);

        // blockRect is in document coordinates, translate it to be relative to
        // the viewport. Then we want the first block that starts after the current
        // scrollbar position.
        blockRect.translate(viewportGeometry.topLeft());
        if (blockRect.y() > verticalScrollBar()->sliderPosition()) {
            return it;
        }
    }
    return QTextBlock();
}

void Editor::lineNumberGutterPaintEvent(QWidget* gutter, QPaintEvent* event)
{
    QColor bgColor(38,35,58);
    QColor fgColor(144,140,170);

    QPainter painter(gutter);
    painter.fillRect(event->rect(), bgColor);

    QTextBlock block = getFirstVisibleBlock();
    int blockNumberUnderCursor = textCursor().blockNumber();

    QTextDocument* doc = document();
    QRect viewportGeometry = viewport()->geometry();

    qreal additionalMargin;
    if (block.blockNumber() == 0) {
        additionalMargin = doc->documentMargin() - 1 - verticalScrollBar()->sliderPosition();
    }
    else {
        // Getting the height of the visible part of the previous "non entirely visible" block
        QTextBlock prevBlock = block.previous();
        QRectF prevBlockRect = doc->documentLayout()->blockBoundingRect(prevBlock);
        prevBlockRect.translate(0, -verticalScrollBar()->sliderPosition());

        additionalMargin = prevBlockRect.intersected(viewportGeometry).height();
    }

    qreal top = viewportGeometry.top() + additionalMargin;
    qreal bottom = top + doc->documentLayout()->blockBoundingRect(block).height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            QString number = QString::number(block.blockNumber() + 1);

            painter.setPen(fgColor);

            QFont f = gutter->font();
            if (block.blockNumber() == blockNumberUnderCursor) {
                f.setWeight(QFont::ExtraBold);
            }
            painter.setFont(f);

            int textFlags;
            int textOffset;
            if (gutter == d_leftLineNumberGutter) {
                textFlags = Qt::AlignRight;
                textOffset = -5;
            }
            else {
                textFlags = Qt::AlignLeft;
                textOffset = 5;
            }
            if (layoutDirection() == Qt::RightToLeft) {
                textOffset *= -1;
            }

            QRectF r(textOffset, top, gutter->width(), painter.fontMetrics().height());
            painter.drawText(r, textFlags, number);
        }

        block = block.next();
        top = bottom;
        bottom = top + doc->documentLayout()->blockBoundingRect(block).height();
    }
}

}
