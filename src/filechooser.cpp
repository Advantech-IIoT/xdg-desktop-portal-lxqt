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
#include <QLoggingCategory>
#include <QMimeDatabase>
#include <QUrl>
#include <QDBusObjectPath>

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
// used for options - choices
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::Choice)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::Choices)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::Option)
Q_DECLARE_METATYPE(LXQt::FileChooserPortal::OptionList)

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

    QDBusArgument &operator<<(QDBusArgument &arg, const FileChooserPortal::Choice &choice)
    {
        arg.beginStructure();
        arg << choice.id << choice.value;
        arg.endStructure();
        return arg;
    }

    const QDBusArgument &operator>>(const QDBusArgument &arg, FileChooserPortal::Choice &choice)
    {
        QString id;
        QString value;
        arg.beginStructure();
        arg >> id >> value;
        choice.id = id;
        choice.value = value;
        arg.endStructure();
        return arg;
    }

    QDBusArgument &operator<<(QDBusArgument &arg, const FileChooserPortal::Option &option)
    {
        arg.beginStructure();
        arg << option.id << option.label << option.choices << option.initialChoiceId;
        arg.endStructure();
        return arg;
    }

    const QDBusArgument &operator>>(const QDBusArgument &arg, FileChooserPortal::Option &option)
    {
        QString id;
        QString label;
        FileChooserPortal::Choices choices;
        QString initialChoiceId;
        arg.beginStructure();
        arg >> id >> label >> choices >> initialChoiceId;
        option.id = id;
        option.label = label;
        option.choices = choices;
        option.initialChoiceId = initialChoiceId;
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
        qDBusRegisterMetaType<Choice>();
        qDBusRegisterMetaType<Choices>();
        qDBusRegisterMetaType<Option>();
        qDBusRegisterMetaType<OptionList>();
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

        // open directory
        if (directory && !options.contains(QStringLiteral("choices"))) {
            QString newTitle = title;
            // change title
            if (newTitle.contains(QString(STR_FILE))) {
                newTitle.replace(QString(STR_FILE), QString(STR_DIRECTORY));
            }
            // construct qtfiledialog command
            QString qCmd = QString(FILEDIALOG_CMD_WITH_TITLE).arg(newTitle);
            // add directory argument
            qCmd.append(QString(DIRECTORY_ARG));
            qCDebug(XdgDesktopPortalLxqtFileChooser) << "    command: " << qCmd;
            string cmd = qCmd.toStdString();
            auto ret = execute_cmd(cmd.c_str());
            // parse result
            QStringList directories;
            directories << QString::fromStdString(ret.first);
            if (directories.isEmpty()) {
                qCDebug(XdgDesktopPortalLxqtFileChooser) << "Failed to open directory: no local directory selected";
                return 2;
            }

            results.insert(QStringLiteral("uris"), directories);
            results.insert(QStringLiteral("writable"), true);

            return 0;
        }

        // construct qtfiledialog command
        QString qCmd = QString(FILEDIALOG_CMD_WITH_TITLE).arg(title);
        if (!nameFilters.isEmpty()) {
            // nameFilters is string list
            // ex: [ "Custom Files (*.jpg *.JPG *.png *.PNG)", "All Files (*.*)" ]
            // join string list by comma for qtfiledialog argument
            qCmd.append(QString(NAMEFILTER_ARG).arg(nameFilters.join(STR_COMMA)));
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
            // 'accept_label' allows mnemonic underlines, but Qt uses '&' character, so replace/escape accordingly
            // to keep literal '&'s and transform mnemonic underlines to the Qt equivalent using '&' for mnemonic
            acceptLabel.replace(QChar::fromLatin1('&'), QStringLiteral("&&"));
            const int mnemonic_pos = acceptLabel.indexOf(QChar::fromLatin1('_'));
            if (mnemonic_pos != -1) {
                acceptLabel.replace(mnemonic_pos, 1, QChar::fromLatin1('&'));
            }
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
