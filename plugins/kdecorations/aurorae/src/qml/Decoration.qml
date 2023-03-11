/*
SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
import QtQuick
import org.kde.kwin.decoration

Item {
    property QtObject borders: Borders {
        objectName: "borders"
    }
    property QtObject maximizedBorders: Borders {
        objectName: "maximizedBorders"
    }
    property QtObject extendedBorders: Borders {
        objectName: "extendedBorders"
    }
    property QtObject padding: Borders {
        objectName: "padding"
    }
    property bool alpha: true
    width: decoration.client.width + decoration.borderLeft + decoration.borderRight + (decoration.client.maximized ? 0 : (padding.left + padding.right))
    height: decoration.client.height + decoration.borderTop + decoration.borderBottom + (decoration.client.maximized ? 0 : (padding.top + padding.bottom))
}
