#pragma once
#include <cmath>
#include <limits>
#include <initializer_list>
#include <glm/glm.hpp>

namespace cnc {
static_assert(std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits==53);
// Conservative exact predicate on the supplied binary64 coordinates. No epsilon.
// IEEE error-free subtraction and FMA product residuals prove equality; uncertain
// (inexact differences, extreme exponents) cases simply retain the original path.
inline bool exact_straight_extension(glm::dvec3 a, glm::dvec3 b, glm::dvec3 c) {
    int moving = 0;
    for (int i=0; i<3; ++i) {
        for (double x : {a[i], b[i], c[i]})
            if (!std::isfinite(x) || std::abs(x)>1e12) return false;
        if (!((a[i]<=b[i] && b[i]<=c[i]) || (a[i]>=b[i] && b[i]>=c[i]))) return false;
        moving += a[i]!=c[i];
    }
    if (moving<=1 || a==b || b==c) return true;
    glm::dvec3 u, v;
    auto difference = [](double x, double y, double& d) {
        d=x-y;
        const double yvirtual=x-d, xvirtual=d+yvirtual;
        return ((x-xvirtual)+(yvirtual-y))==0;
    };
    for (int i=0; i<3; ++i)
        if (!difference(b[i],a[i],u[i]) || !difference(c[i],b[i],v[i])) return false;
    // Restrict nonzero operands to a range where the full product residual is
    // representable (including its lowest possible bit); no underflow ambiguity.
    for (int i=0; i<3; ++i) for (double x : {u[i],v[i]})
        if (x!=0 && (std::abs(x)<0x1p-400 || std::abs(x)>0x1p400)) return false;
    for (int i=0; i<3; ++i) {
        const int j=(i+1)%3;
        const double p=u[i]*v[j], q=u[j]*v[i];
        if (p!=q || std::fma(u[i],v[j],-p)!=std::fma(u[j],v[i],-q)) return false;
    }
    return true;
}
} // namespace cnc
