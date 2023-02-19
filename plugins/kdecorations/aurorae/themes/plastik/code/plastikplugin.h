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
#ifndef PLASTIK_PLUGIN_H
#define PLASTIK_PLUGIN_H

#include <QQmlExtensionPlugin>

class PlastikPlugin : public QQmlExtensionPlugin
{
    Q_PLUGIN_METADATA(IID "org.kde.kwin.decorations.plastik")
    Q_OBJECT
public:
    void registerTypes(const char *uri) override;
    void initializeEngine(QQmlEngine *engine, const char *uri) override;
};

#endif // PLASTIK_PLUGIN_H
