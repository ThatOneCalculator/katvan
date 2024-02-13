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
#include "katvan_spellchecker.h"

#include <hunspell.hxx>

#include <QApplication>
#include <QDir>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QMessageBox>
#include <QMetaObject>
#include <QMutex>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextBoundaryFinder>
#include <QTextStream>
#include <QThread>

namespace katvan {

static constexpr size_t SUGGESTIONS_CACHE_SIZE = 25;

QString SpellChecker::s_personalDictionaryLocation;

struct LoadedSpeller
{
    LoadedSpeller(const char* affPath, const char* dicPath)
        : speller(affPath, dicPath) {}

    Hunspell speller;
    QMutex mutex;
};

SpellChecker::SpellChecker(QObject* parent)
    : QObject(parent)
    , d_suggestionsCache(SUGGESTIONS_CACHE_SIZE)
{
    d_suggestionThread = new QThread(this);
    d_suggestionThread->setObjectName("SuggestionThread");

    QString loc = s_personalDictionaryLocation;
    if (loc.isEmpty()) {
        loc = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    }

    d_personalDictionaryPath = loc + QDir::separator() + "/personal.dic";
    loadPersonalDictionary();

    d_watcher = new QFileSystemWatcher(this);
    d_watcher->addPath(d_personalDictionaryPath);
    connect(d_watcher, &QFileSystemWatcher::fileChanged, this, &SpellChecker::personalDictionaryFileChanged);
}

SpellChecker::~SpellChecker()
{
    if (d_suggestionThread->isRunning()) {
        d_suggestionThread->quit();
        d_suggestionThread->wait();
    }
}

/**
 * Scan system and executable-local locations for Hunspell dictionaries,
 * which are a pair of *.aff and *.dic files with the same base name.
 */
QMap<QString, QString> SpellChecker::findDictionaries()
{
    QStringList dictDirs;
    dictDirs.append(QCoreApplication::applicationDirPath() + "/hunspell");

    QStringList systemDirs = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    for (const QString& dir : systemDirs) {
        dictDirs.append(dir + "/hunspell");
    }

    QStringList nameFilters = { "*.aff" };
    QMap<QString, QString> affFiles;

    for (const QString& dirName : dictDirs) {
        QDir dir(dirName);
        QFileInfoList affixFiles = dir.entryInfoList(nameFilters, QDir::Files);

        for (QFileInfo& affInfo : affixFiles) {
            QString dictName = affInfo.baseName();
            QString dicFile = dirName + "/" + dictName + ".dic";
            if (!QFileInfo::exists(dicFile)) {
                continue;
            }

            if (!affFiles.contains(dictName)) {
                affFiles.insert(dictName, affInfo.absoluteFilePath());
            }
        }
    }
    return affFiles;
}

QString SpellChecker::dictionaryDisplayName(const QString& dictName)
{
    QLocale locale(dictName);
    if (locale.language() == QLocale::C) {
        return tr("Unknown");
    }

    return QStringLiteral("%1 (%2)").arg(
        QLocale::languageToString(locale.language()),
        QLocale::territoryToString(locale.territory())
    );
}

void SpellChecker::setPersonalDictionaryLocation(const QString& dirPath)
{
    s_personalDictionaryLocation = dirPath;
}

void SpellChecker::setCurrentDictionary(const QString& dictName, const QString& dictAffFile)
{
    if (!dictName.isEmpty() && !d_spellers.contains(dictName)) {
        QString dicFile = QFileInfo(dictAffFile).path() + "/" + dictName + ".dic";

        QByteArray affPath = dictAffFile.toLocal8Bit();
        QByteArray dicPath = dicFile.toLocal8Bit();
        d_spellers.emplace(dictName, std::make_unique<LoadedSpeller>(affPath.data(), dicPath.data()));
    }

    d_currentDictName = dictName;
    d_suggestionsCache.clear();
}

bool SpellChecker::checkWord(Hunspell& speller, const QString& word)
{
    QString normalizedWord = word.normalized(QString::NormalizationForm_D);
    if (d_personalDictionary.contains(normalizedWord)) {
        return true;
    }

    return speller.spell(word.toStdString());
}

QList<std::pair<size_t, size_t>> SpellChecker::checkSpelling(const QString& text)
{
    QList<std::pair<size_t, size_t>> result;
    if (d_currentDictName.isEmpty()) {
        return result;
    }

    LoadedSpeller* speller = d_spellers[d_currentDictName].get();
    if (!speller->mutex.tryLock()) {
        // Do not block the UI event loop! If we can't take the speller
        // lock (because suggestions are being generated at the moment),
        // just pretend there are no spelling mistakes here.
        return result;
    }

    QTextBoundaryFinder boundryFinder(QTextBoundaryFinder::Word, text);

    qsizetype prevPos = 0;
    while (boundryFinder.toNextBoundary() >= 0) {
        qsizetype pos = boundryFinder.position();
        if (boundryFinder.boundaryReasons() & QTextBoundaryFinder::EndOfItem) {
            QString word = text.sliced(prevPos, pos - prevPos);
            bool ok = checkWord(speller->speller, word);
            if (!ok) {
                result.append(std::make_pair<size_t, size_t>(prevPos, pos - prevPos));
            }
        }
        prevPos = pos;
    }

    speller->mutex.unlock();
    return result;
}

void SpellChecker::addToPersonalDictionary(const QString& word)
{
    d_personalDictionary.insert(word.normalized(QString::NormalizationForm_D));
    flushPersonalDictionary();
}

void SpellChecker::flushPersonalDictionary()
{
    QDir dictDir = QFileInfo(d_personalDictionaryPath).dir();
    if (!dictDir.exists()) {
        dictDir.mkpath(".");
    }

    QSaveFile file(d_personalDictionaryPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(
            QApplication::activeWindow(),
            QCoreApplication::applicationName(),
            tr("Saving personal dictionary to %1 failed: %2").arg(d_personalDictionaryPath, file.errorString()));

        return;
    }

    QTextStream stream(&file);
    for (const QString& word : std::as_const(d_personalDictionary)) {
        stream << word << "\n";
    }
    file.commit();
}

void SpellChecker::loadPersonalDictionary()
{
    if (!QFileInfo::exists(d_personalDictionaryPath)) {
        return;
    }

    QFile file(d_personalDictionaryPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::critical(
            QApplication::activeWindow(),
            QCoreApplication::applicationName(),
            tr("Loading personal dictionary from %1 failed: %2").arg(d_personalDictionaryPath, file.errorString()));

        return;
    }

    d_personalDictionary.clear();

    QString line;
    QTextStream stream(&file);
    while (stream.readLineInto(&line)) {
        if (line.isEmpty()) {
            continue;
        }
        d_personalDictionary.insert(line.normalized(QString::NormalizationForm_D));
    }
}

void SpellChecker::personalDictionaryFileChanged()
{
    qDebug() << "Personal dictionary file changed on disk";
    loadPersonalDictionary();

    if (!d_watcher->files().contains(d_personalDictionaryPath)) {
        d_watcher->addPath(d_personalDictionaryPath);
    }
}

void SpellChecker::requestSuggestions(const QString& word, int position)
{
    if (d_currentDictName.isEmpty()) {
        qWarning() << "Asked for suggestions, but no dictionary is active!";
        return;
    }

    if (d_suggestionsCache.contains(word)) {
        Q_EMIT suggestionsReady(word, position, *d_suggestionsCache.object(word));
        return;
    }

    if (!d_suggestionThread->isRunning()) {
        qDebug() << "Starting suggestion generation thread";
        d_suggestionThread->start();
    }

    LoadedSpeller* speller = d_spellers[d_currentDictName].get();

    SpellingSuggestionsWorker* worker = new SpellingSuggestionsWorker(speller, word, position);
    worker->moveToThread(d_suggestionThread);

    connect(worker, &SpellingSuggestionsWorker::suggestionsReady, this, &SpellChecker::suggestionsWorkerDone);
    QMetaObject::invokeMethod(worker, &SpellingSuggestionsWorker::process, Qt::QueuedConnection);
}

void SpellChecker::suggestionsWorkerDone(QString word, int position, QStringList suggestions)
{
    d_suggestionsCache.insert(word, new QStringList(suggestions));

    Q_EMIT suggestionsReady(word, position, suggestions);
}

void SpellingSuggestionsWorker::process()
{
    std::vector<std::string> suggestions;
    {
        QMutexLocker locker{ &d_speller->mutex };
        suggestions = d_speller->speller.suggest(d_word.toStdString());
    }

    QStringList result;
    result.reserve(suggestions.size());
    for (const auto& s : suggestions) {
        result.append(QString::fromStdString(s));
    }

    Q_EMIT suggestionsReady(d_word, d_pos, result);

    deleteLater();
}

}
