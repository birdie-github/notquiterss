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
#ifndef OPMLEXPORTER_H
#define OPMLEXPORTER_H

class QIODevice;
class QTreeView;

namespace OpmlExporter {
// Writes the full database feed snapshot, independent of the active filter.
// appearanceView supplies the existing FeedsModel's presentation dependency.
// The caller opens and closes output.
void write(QIODevice &output, QTreeView &appearanceView);
}

#endif // OPMLEXPORTER_H
