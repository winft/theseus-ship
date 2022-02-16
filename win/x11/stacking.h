#include "group.h"
#include "window.h"

#include "base/platform.h"
#include "screens.h"
#include "toplevel.h"

#include <deque>

namespace KWin::win::x11
{

/**
 * Group windows by layer, than flatten to a list.
 * @param list container of windows to sort
 */
template<typename Container>
std::vector<Toplevel*> sort_windows_by_layer(Container const& list)
{
    constexpr size_t layer_count = static_cast<int>(layer::count);
    std::deque<Toplevel*> layers[layer_count];

    // build the order from layers
    QVector<QMap<group*, layer>> minimum_layer(
        std::max<size_t>(kwinApp()->get_base().get_outputs().size(), 1));

    for (auto const& win : list) {
        auto l = win->layer();

        auto const screen = win->screen();
        auto c = qobject_cast<window*>(win);

        QMap<group*, layer>::iterator mLayer = minimum_layer[screen].find(c ? c->group() : nullptr);
        if (mLayer != minimum_layer[screen].end()) {
            // If a window is raised above some other window in the same window group
            // which is in the ActiveLayer (i.e. it's fulscreened), make sure it stays
            // above that window (see #95731).
            if (*mLayer == layer::active
                && (static_cast<int>(l) > static_cast<int>(layer::below))) {
                l = layer::active;
            }
            *mLayer = l;
        } else if (c) {
            minimum_layer[screen].insertMulti(c->group(), l);
        }
        layers[static_cast<size_t>(l)].push_back(win);
    }

    std::vector<Toplevel*> sorted;

    for (auto lay = static_cast<size_t>(layer::first); lay < layer_count; ++lay) {
        sorted.insert(sorted.end(), layers[lay].begin(), layers[lay].end());
    }

    return sorted;
}

}
