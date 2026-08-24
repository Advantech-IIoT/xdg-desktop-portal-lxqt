/* BEGIN_COMMON_COPYRIGHT_HEADER
 * (c)LGPL2+
 *
 * LXQt - a lightweight, Qt based, desktop toolset
 * https://lxqt-project.org
 *
 * Copyright: 2016-2018 Red Hat Inc
 * Copyright: 2016-2018 Jan Grulich <jgrulich@redhat.com>
 * Copyright: 2021~ LXQt team
 * Authors:
 *   Palo Kisa <palo.kisa@gmail.com>
 *
 * This program or library is free software; you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General
 * Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA
 *
 * END_COMMON_COPYRIGHT_HEADER */
// Copyright (C) 2024 The Advantech Company Ltd.
// SPDX-License-Identifier: GPL-3.0-only

#include "filechooser.h"
#include "utils.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QFile>
#include <QLayout>
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QUrl>
#include <QDBusObjectPath>
#include <QRegularExpression>

#define STR_COMMA ","
#define STR_FILE "File"
#define STR_DIRECTORY "Directory"
#define FILEDIALOG_CMD_WITH_TITLE "/usr/local/bin/qtfiledialog -t '%1'"
#define NAMEFILTER_ARG " -f '%1'"
#define DIRECTORY_ARG " -d"
#define SAVEFILE_ARG " -s"
#define STDERR_TO_NULL " 2>/dev/null"

// Keep in sync with qflatpakfiledialog from flatpak-platform-plugin
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::Filter)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::Filters)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::FilterList)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::FilterListList)

using namespace std;

namespace LXQt
{
    Q_LOGGING_CATEGORY(XdgDesktopPortalLxqtFileChooser, "xdp-lxqt-file-chooser")


    QDBusArgument &operator<<(QDBusArgument &arg, const FileChooserPortal::Filter &filter)
    {
        arg.beginStructure();
        arg << filter.type << filter.filterString;
        arg.endStructure();
        return arg;
    }

    const QDBusArgument &operator>>(const QDBusArgument &arg, FileChooserPortal::Filter &filter)
    {
        uint type;
        QString filterString;
        arg.beginStructure();
        arg >> type >> filterString;
        filter.type = type;
        filter.filterString = filterString;
        arg.endStructure();

        return arg;
    }

    QDBusArgument &operator<<(QDBusArgument &arg, const FileChooserPortal::FilterList &filterList)
    {
        arg.beginStructure();
        arg << filterList.userVisibleName << filterList.filters;
        arg.endStructure();
        return arg;
    }

    const QDBusArgument &operator>>(const QDBusArgument &arg, FileChooserPortal::FilterList &filterList)
    {
        QString userVisibleName;
        FileChooserPortal::Filters filters;
        arg.beginStructure();
        arg >> userVisibleName >> filters;
        filterList.userVisibleName = userVisibleName;
        filterList.filters = filters;
        arg.endStructure();

        return arg;
    }

    FileChooserPortal::FileChooserPortal(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
        qDBusRegisterMetaType<Filter>();
        qDBusRegisterMetaType<Filters>();
        qDBusRegisterMetaType<FilterList>();
        qDBusRegisterMetaType<FilterListList>();
    }

    FileChooserPortal::~FileChooserPortal()
    {
    }

    // The portal may send us null terminated strings. Make sure to strip the extranous \0
    // in favor of the implicit \0.
    // QByteArrays are implicitly terminated already.
    static QUrl decodeFileName(const QByteArray &name)
    {
        QByteArray decodedName = name;
        while (decodedName.endsWith('\0')) {
            decodedName.chop(1);
        }
        QString str = QFile::decodeName(decodedName);
        if (!str.isEmpty()) {
            return QUrl::fromLocalFile(str);
        }
        return QUrl();
    }

    // make "[pP][nN][gG]" to simple png
    QString FileChooserPortal::collapseCaseInsensitive(const QString &pattern)
    {
        QString result;
        result.reserve(pattern.size());

        int i = 0;
        const int n = pattern.size();
        while (i < n) {
            // Handle [aA] / [AA] / [aa]
            if (i + 3 < n &&
                pattern[i] == QLatin1Char('[') &&
                pattern[i + 3] == QLatin1Char(']')) {
                // aA to a
                const QChar c1 = pattern[i + 1];
                const QChar c2 = pattern[i + 2];
                if (c1.toLower() == c2.toLower()) {
                    result += c1.toLower();
                    i += 4;
                    continue;
                }
            }
            result += pattern[i].toLower();
            ++i;
        }

        return result;
    }

    QStringList FileChooserPortal::convertPortalNameFiltersToQml(const QStringList &input)
    {
        QStringList result;
        result.reserve(input.size());

        // match namefilter pattern "Label (pattern1 pattern2 ...)"
        // ex: [ "Custom Files (*.[pP][nN][gG] *.[pP][jJ][pP][eE][gG] *.[pP][jJ][pP] *.[jJ][fF][iI][fF] *.[jJ][pP][gG] *.[jJ][pP][eE][gG]),All Files (*.*)" ]
        static const QRegularExpression groupRe(QStringLiteral(R"(^\s*(.*?)\s*\((.*)\)\s*$)"));

        for (const QString &rawItem : input) {
            QString rawGroup = rawItem.trimmed();

            // trim ""
            if (rawGroup.size() >= 2 &&
                rawGroup.front() == QLatin1Char('"') &&
                rawGroup.back() == QLatin1Char('"')) {
                rawGroup = rawGroup.mid(1, rawGroup.size() - 2);
            }

            const QRegularExpressionMatch match = groupRe.match(rawGroup);
            if (!match.hasMatch()) {
                qWarning() << "cannot parse filter item, skipping:" << rawGroup;
                continue;
            }

            const QString label = match.captured(1).trimmed();
            const QString extsPart = match.captured(2).trimmed();

            const QStringList rawExts = extsPart.split(QLatin1Char(' '), Qt::SkipEmptyParts);

            QStringList cleanExts;
            cleanExts.reserve(rawExts.size());
            for (const QString &rawExt : rawExts) {
                cleanExts << collapseCaseInsensitive(rawExt);
            }

            // trim duplicate item (e.g., if both *.jpg and *.JPG appear, they will be duplicated after expansion)
            QStringList dedupExts;
            for (const QString &ext : cleanExts) {
                bool exists = false;
                for (const QString &e : dedupExts) {
                    if (e.compare(ext, Qt::CaseInsensitive) == 0) {
                        exists = true;
                        break;
                    }
                }
                if (!exists)
                    dedupExts << ext;
            }

            const QString filterStr =
                QStringLiteral("%1 (%2)").arg(label, dedupExts.join(QLatin1Char(' ')));

            result << filterStr;
        }

        return result;
    }

    uint FileChooserPortal::OpenFile(const QDBusObjectPath &handle,
            const QString &app_id,
            const QString &parent_window,
            const QString &title,
            const QVariantMap &options,
            QVariantMap &results)
    {
        Q_UNUSED(app_id);

        qCDebug(XdgDesktopPortalLxqtFileChooser) << "OpenFile called with parameters:";
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    handle: " << handle.path();
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    parent_window: " << parent_window;
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    title: " << title;
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    options: " << options;

        bool directory = false;
        bool modalDialog = true;
        bool multipleFiles = false;
        QUrl currentFolder;
        QStringList nameFilters;
        QString selectedNameFilter;
        // mapping between filter strings and actual filters
        QMap<QString, FilterList> allFilters;

        const QString acceptLabel = ExtractAcceptLabel(options);

        if (options.contains(QStringLiteral("modal"))) {
            modalDialog = options.value(QStringLiteral("modal")).toBool();
        }

        if (options.contains(QStringLiteral("multiple"))) {
            multipleFiles = options.value(QStringLiteral("multiple")).toBool();
        }

        if (options.contains(QStringLiteral("directory"))) {
            directory = options.value(QStringLiteral("directory")).toBool();
        }

        if (options.contains(QStringLiteral("current_folder"))) {
            currentFolder = decodeFileName(options.value(QStringLiteral("current_folder")).toByteArray());
        }

        ExtractFilters(options, nameFilters, allFilters, selectedNameFilter);
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    nameFilters: " << nameFilters.join(STR_COMMA);

        // construct qtfiledialog command
        QString qCmd = QString(FILEDIALOG_CMD_WITH_TITLE).arg(title);
        if (!nameFilters.isEmpty()) {
            // nameFilters is string list
            // ex: [ "Custom Files (*.[pP][nN][gG] *.[pP][jJ][pP][eE][gG] *.[pP][jJ][pP] *.[jJ][fF][iI][fF] *.[jJ][pP][gG] *.[jJ][pP][eE][gG]),All Files (*.*)" ]
            // join string list by comma for qtfiledialog argument
            const QStringList qmlFilters = convertPortalNameFiltersToQml(nameFilters);
            qCmd.append(QString(NAMEFILTER_ARG).arg(qmlFilters.join(STR_COMMA)));
        }
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    command: " << qCmd;
        string cmd = qCmd.toStdString();
        auto ret = execute_cmd(cmd.c_str());
        // parse result
        QStringList files;
        files << QString::fromStdString(ret.first);

        if (files.isEmpty()) {
            qCDebug(XdgDesktopPortalLxqtFileChooser) << "Failed to open file: no local file selected";
            return 2;
        }

        results.insert(QStringLiteral("uris"), files);
        results.insert(QStringLiteral("writable"), true);

        return 0;
    }

    uint FileChooserPortal::SaveFile(const QDBusObjectPath &handle,
            const QString &app_id,
            const QString &parent_window,
            const QString &title,
            const QVariantMap &options,
            QVariantMap &results)
    {
        Q_UNUSED(app_id);

        qCDebug(XdgDesktopPortalLxqtFileChooser) << "SaveFile called with parameters:";
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    handle: " << handle.path();
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    parent_window: " << parent_window;
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    title: " << title;
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    options: " << options;

        bool modalDialog = true;
        QString currentName;
        QUrl currentFolder;
        QUrl currentFile;
        QStringList nameFilters;
        QString selectedNameFilter;
        // mapping between filter strings and actual filters
        QMap<QString, FilterList> allFilters;

        if (options.contains(QStringLiteral("modal"))) {
            modalDialog = options.value(QStringLiteral("modal")).toBool();
        }

        const QString acceptLabel = ExtractAcceptLabel(options);

        if (options.contains(QStringLiteral("current_name"))) {
            currentName = options.value(QStringLiteral("current_name")).toString();
        }

        if (options.contains(QStringLiteral("current_folder"))) {
            currentFolder = decodeFileName(options.value(QStringLiteral("current_folder")).toByteArray());
        }

        if (options.contains(QStringLiteral("current_file"))) {
            currentFile = decodeFileName(options.value(QStringLiteral("current_file")).toByteArray());
        }

        ExtractFilters(options, nameFilters, allFilters, selectedNameFilter);

        // construct qtfiledialog command
        QString qCmd = QString(FILEDIALOG_CMD_WITH_TITLE).arg(title);
        if (!nameFilters.isEmpty()) {
            // nameFilters is string list
            // ex: [ "Custom Files (*.jpg *.JPG *.png *.PNG)", "All Files (*.*)" ]
            // join string list by comma for qtfiledialog argument
            qCmd.append(QString(NAMEFILTER_ARG).arg(nameFilters.join(STR_COMMA)));
        }
        // add save file argument
        qCmd.append(QString(SAVEFILE_ARG));
        qCDebug(XdgDesktopPortalLxqtFileChooser) << "    command: " << qCmd;
        string cmd = qCmd.toStdString();
        auto ret = execute_cmd(cmd.c_str());
        // parse result
        QStringList files;
        files << QString::fromStdString(ret.first);

        if (files.isEmpty()) {
            qCDebug(XdgDesktopPortalLxqtFileChooser) << "Failed to open file: no local file selected";
            return 2;
        }

        results.insert(QStringLiteral("uris"), files);

        return 0;
    }

    QString FileChooserPortal::ExtractAcceptLabel(const QVariantMap &options)
    {
        QString acceptLabel;
        if (options.contains(QStringLiteral("accept_label"))) {
            acceptLabel = options.value(QStringLiteral("accept_label")).toString();
            //Utils::convertGtkMnemonic(acceptLabel);
        }
        return acceptLabel;
    }

    void FileChooserPortal::ExtractFilters(const QVariantMap &options,
            QStringList &nameFilters,
            QMap<QString, FilterList> &allFilters,
            QString &selectedNameFilter)
    {
        if (options.contains(QStringLiteral("filters"))) {
            const FilterListList filterListList = qdbus_cast<FilterListList>(options.value(QStringLiteral("filters")));
            for (const FilterList &filterList : filterListList) {
                QStringList filterStrings;
                for (const Filter &filterStruct : filterList.filters) {
                    if (filterStruct.type == 0) {
                        filterStrings << filterStruct.filterString;
                    } else {
                        filterStrings << NameFiltersForMimeType(filterStruct.filterString);
                    }
                }

                if (!filterStrings.isEmpty()) {
                    const QString filterString = filterStrings.join(QLatin1Char(' '));
                    const QString nameFilter = QStringLiteral("%2 (%1)").arg(filterString, filterList.userVisibleName);
                    nameFilters << nameFilter;
                    allFilters[nameFilter] = filterList;
                }
            }
        }

        if (options.contains(QStringLiteral("current_filter"))) {
            FilterList filterList = qdbus_cast<FilterList>(options.value(QStringLiteral("current_filter")));
            if (filterList.filters.size() == 1) {
                QStringList filterStrings;
                Filter filterStruct = filterList.filters.at(0);
                if (filterStruct.type == 0) {
                    filterStrings << filterStruct.filterString;
                } else {
                    filterStrings << NameFiltersForMimeType(filterStruct.filterString);
                }

                if (!filterStrings.isEmpty()) {
                    // make the relevant entry the first one in the list of filters,
                    // since that is the one that gets preselected by KFileWidget::setFilter
                    const QString filterString = filterStrings.join(QLatin1Char(' '));
                    const QString nameFilter = QStringLiteral("%2 (%1)").arg(filterString, filterList.userVisibleName);
                    nameFilters.removeAll(nameFilter);
                    nameFilters.push_front(nameFilter);
                    selectedNameFilter = nameFilter;
                }
            } else {
                qCDebug(XdgDesktopPortalLxqtFileChooser) << "Ignoring 'current_filter' parameter with 0 or multiple filters specified.";
            }
        }
    }

    QStringList FileChooserPortal::NameFiltersForMimeType(const QString &mimeType)
    {
        QMimeDatabase db;
        QMimeType mime(db.mimeTypeForName(mimeType));

        if (mime.isValid()) {
            if (mime.isDefault()) {
                return QStringList("*");
            }
            return mime.globPatterns();
        }
        return QStringList();
    }
}
