/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * BP4Reader.h
 *
 *  Created on: Aug 1, 2018
 *      Author: Lipeng Wan wanl@ornl.gov
 */

#ifndef ADIOS2_ENGINE_BP4_BP4READER_H_
#define ADIOS2_ENGINE_BP4_BP4READER_H_

#include "adios2/common/ADIOSConfig.h"
#include "adios2/core/Engine.h"
#include "adios2/toolkit/format/bp4/BP4.h" //format::BP4Deserializer
#include "adios2/toolkit/transportman/TransportMan.h"

namespace adios2
{
namespace core
{
namespace engine
{

class BP4Reader : public Engine
{

public:
    /**
     * Unique constructor
     * @param io
     * @param name
     * @param openMode only read
     * @param mpiComm
     */
    BP4Reader(IO &io, const std::string &name, const Mode mode,
              MPI_Comm mpiComm);

    virtual ~BP4Reader() = default;

    StepStatus BeginStep(StepMode mode = StepMode::Read,
                         const float timeoutSeconds = -1.0) final;

    size_t CurrentStep() const final;

    void EndStep() final;

    void PerformGets() final;

private:
    format::BP4Deserializer m_BP4Deserializer;
    /* transport manager for metadata file */
    transportman::TransportMan m_MDFileManager;
    size_t m_MDFileProcessedSize = 0;

    /* transport manager for managing data file(s) */
    transportman::TransportMan m_DataFileManager;

    /* transport manager for managing the metadata index file */
    transportman::TransportMan m_MDIndexFileManager;
    size_t m_MDIndexFileProcessedSize = 0;

    /** used for per-step reads, TODO: to be moved to BP4Deserializer */
    size_t m_CurrentStep = 0;
    bool m_FirstStep = true;

    void Init();
    void InitTransports();
    void InitBuffer();

    /** Read in more metadata if exist (throwing away old).
     *  For streaming only.
     *  @return pair of sizes if new metadata was read in
     *  sizes.first = size of new content from Index Table
     *  sizes.second = size of new content from Metadata File
     */
    std::pair<size_t, size_t> UpdateBuffer();

    /** Process the new metadata coming in (in UpdateBuffer)
     *  @param newIdxSize: the size of the new content from Index Table
     *  @param neMDSize: the size of new content from Metadata File
     */
    void ProcessMetadataForNewSteps(const size_t newIdxSize,
                                    const size_t newMDSize);

    /** Check the active status flag in index file.
     *  @return true if writer is still active
     *  it sets BP4Deserialized.m_WriterIsActive
     */
    bool CheckWriterActive();

    /** Check for new steps withing timeout and only if writer is active.
     *  @return the status flag
     *  Used by BeginStep() to get new steps from file when it reaches the
     *  end of steps in memory.
     */
    StepStatus CheckForNewSteps(float timeoutSeconds);

#define declare_type(T)                                                        \
    void DoGetSync(Variable<T> &, T *) final;                                  \
    void DoGetDeferred(Variable<T> &, T *) final;
    ADIOS2_FOREACH_STDTYPE_1ARG(declare_type)
#undef declare_type

    void DoClose(const int transportIndex = -1) final;

    template <class T>
    void GetSyncCommon(Variable<T> &variable, T *data);

    template <class T>
    void GetDeferredCommon(Variable<T> &variable, T *data);

    template <class T>
    void ReadVariableBlocks(Variable<T> &variable);

#define declare_type(T)                                                        \
    std::map<size_t, std::vector<typename Variable<T>::Info>>                  \
    DoAllStepsBlocksInfo(const Variable<T> &variable) const final;             \
                                                                               \
    std::vector<std::vector<typename Variable<T>::Info>>                       \
    DoAllRelativeStepsBlocksInfo(const Variable<T> &) const final;             \
                                                                               \
    std::vector<typename Variable<T>::Info> DoBlocksInfo(                      \
        const Variable<T> &variable, const size_t step) const final;

    ADIOS2_FOREACH_STDTYPE_1ARG(declare_type)
#undef declare_type

    size_t DoSteps() const final;
};

} // end namespace engine
} // end namespace core
} // end namespace adios2

#endif /* ADIOS2_ENGINE_BP4_BP4READER_H_ */
