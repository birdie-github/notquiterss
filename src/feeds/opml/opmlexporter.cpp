/* ============================================================
* QuiteRSS is an open-source cross-platform RSS/Atom news feeds reader
* © 2011-2020 QuiteRSS Project
* © 2026 Artem S. Tashkinov <aros@gmx.com> and ChatGPT
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <https://www.gnu.org/licenses/>.
* ============================================================ */
#include <QCoreApplication>
#include "opmlexporter.h"

#include "feeds/feedsmodel.h"

#include <QDateTime>
#include <QIODevice>
#include <QStack>
#include <QTreeView>
#include <QXmlStreamWriter>

void OpmlExporter::write(QIODevice &output, QTreeView &appearanceView)
{
  QXmlStreamWriter xml(&output);
  xml.setAutoFormatting(true);
  xml.writeStartDocument();
  xml.writeStartElement("opml");
  xml.writeAttribute("version", "2.0");
  xml.writeStartElement("head");
  xml.writeTextElement("title", QCoreApplication::applicationName());
  xml.writeTextElement("dateModified", QDateTime::currentDateTime().toString());
  xml.writeEndElement(); // </head>

  xml.writeStartElement("body");

  // Create model and view for export
  // Expand the view to step on every item
  FeedsModel exportTreeModel;
  QTreeView exportTreeView;
  exportTreeView.setModel(&exportTreeModel);
  exportTreeModel.setView(&appearanceView);
  exportTreeView.expandAll();

  QModelIndex index = exportTreeModel.index(0, 0);
  QStack<int> parentIdsStack;
  parentIdsStack.push(0);
  while (index.isValid()) {
    int feedId = exportTreeModel.idByIndex(index);
    int feedParId = exportTreeModel.paridByIndex(index);

    // Parent differs from previouse one - close folder
    while (feedParId != parentIdsStack.top()) {
      xml.writeEndElement();  // "outline" - folder finishes
      parentIdsStack.pop();
    }

    // Folder has found. Open it
    if (exportTreeModel.isFolder(index)) {
      parentIdsStack.push(feedId);
      xml.writeStartElement("outline");  // Folder starts
      xml.writeAttribute("text", exportTreeModel.dataField(index, "text").toString());
    }
    // Feed has found. Save it
    else {
      xml.writeEmptyElement("outline");
      xml.writeAttribute("text",    exportTreeModel.dataField(index, "text").toString());
      xml.writeAttribute("type",    "rss");
      xml.writeAttribute("htmlUrl", exportTreeModel.dataField(index, "htmlUrl").toString());
      xml.writeAttribute("xmlUrl",  exportTreeModel.dataField(index, "xmlUrl").toString());
    }

    index = exportTreeView.indexBelow(index);
  }

  xml.writeEndElement(); // </body>

  xml.writeEndElement(); // </opml>
  xml.writeEndDocument();
}
