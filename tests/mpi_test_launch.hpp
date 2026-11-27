#pragma once

#include <string>
#include <vector>

namespace conway::test {
inline std::vector<std::string> mpiTestFlags() {
#ifdef CONWAY_MPI_USES_HYDRA
    return {};
#else
    return {"--oversubscribe"};
#endif
}

inline std::vector<std::string> mpiTestLocalFlags() {
#ifdef CONWAY_MPI_USES_HYDRA
    return {"-launcher","fork","-hosts","localhost"};
#else
    return {"--oversubscribe","--host","localhost"};
#endif
}
}
