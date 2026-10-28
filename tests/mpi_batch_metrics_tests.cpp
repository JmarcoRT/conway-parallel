#include "core/engines/mpi.hpp"
#include <mpi.h>
int main(int argc,char** argv) {
    MPI_Init(&argc,&argv); int rank=0,size=0;
    MPI_Comm_rank(MPI_COMM_WORLD,&rank); MPI_Comm_size(MPI_COMM_WORLD,&size);
    const double maximum=conway::core::maximum_rank_milliseconds(static_cast<double>(rank+1),MPI_COMM_WORLD);
    const int result=rank==0 && maximum!=size?1:0;
    MPI_Finalize(); return result;
}
