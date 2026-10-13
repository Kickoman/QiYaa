#include "core/track.h"

namespace Core {

QString Track::displayTitle() const {
    return artists.isEmpty() ? title : artists.join(QStringLiteral(", ")) + QStringLiteral(" - ") + title;
}

}  // namespace Core
