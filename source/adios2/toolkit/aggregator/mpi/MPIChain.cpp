/*
 * Distributed under the OSI-approved Apache License, Version 2.0.  See
 * accompanying file Copyright.txt for details.
 *
 * MPIChain.cpp
 *
 *  Created on: Feb 21, 2018
 *      Author: William F Godoy godoywf@ornl.gov
 */
#include "MPIChain.h"

#include "adios2/common/ADIOSMPI.h"
#include "adios2/helper/adiosFunctions.h" //helper::CheckMPIReturn

#include "adios2/toolkit/format/buffer/heap/BufferSTL.h"

namespace adios2
{
namespace aggregator
{

MPIChain::MPIChain() : MPIAggregator() {}

void MPIChain::Init(const size_t subStreams, MPI_Comm parentComm)
{
    InitComm(subStreams, parentComm);
    HandshakeRank(0);
    HandshakeLinks();

    // add a receiving buffer except for the last rank (only sends)
    if (m_Rank < m_Size)
    {
        m_Buffers.emplace_back(new format::BufferSTL()); // just one for now
    }
}

std::vector<std::vector<MPI_Request>>
MPIChain::IExchange(format::Buffer &buffer, const int step)
{
    if (m_Size == 1)
    {
        return std::vector<std::vector<MPI_Request>>();
    }

    format::Buffer &sendBuffer = GetSender(buffer);
    const int endRank = m_Size - 1 - step;
    const bool sender = (m_Rank >= 1 && m_Rank <= endRank) ? true : false;
    const bool receiver = (m_Rank < endRank) ? true : false;

    std::vector<std::vector<MPI_Request>> requests(2);

    if (sender) // sender
    {
        requests[0].resize(1);

        helper::CheckMPIReturn(MPI_Isend(&sendBuffer.m_Position, 1,
                                         ADIOS2_MPI_SIZE_T, m_Rank - 1, 0,
                                         m_Comm, &requests[0][0]),
                               ", aggregation Isend size at iteration " +
                                   std::to_string(step) + "\n");

        // only send data if buffer larger than 0
        if (sendBuffer.m_Position > 0)
        {

            const std::vector<MPI_Request> requestsISend64 = helper::Isend64(
                sendBuffer.Data(), sendBuffer.m_Position, m_Rank - 1, 1, m_Comm,
                ", aggregation Isend64 data at iteration " +
                    std::to_string(step));

            requests[0].insert(requests[0].end(), requestsISend64.begin(),
                               requestsISend64.end());
        }
    }
    // receive size, resize receiving buffer and receive data
    if (receiver)
    {
        size_t bufferSize = 0;
        MPI_Request receiveSizeRequest;
        helper::CheckMPIReturn(MPI_Irecv(&bufferSize, 1, ADIOS2_MPI_SIZE_T,
                                         m_Rank + 1, 0, m_Comm,
                                         &receiveSizeRequest),
                               ", aggregation Irecv size at iteration " +
                                   std::to_string(step) + "\n");

        MPI_Status receiveStatus;
        helper::CheckMPIReturn(
            MPI_Wait(&receiveSizeRequest, &receiveStatus),
            ", aggregation waiting for receiver size at iteration " +
                std::to_string(step) + "\n");

        format::Buffer &receiveBuffer = GetReceiver(buffer);
        ResizeUpdateBuffer(
            bufferSize, receiveBuffer,
            "in aggregation, when resizing receiving buffer to size " +
                std::to_string(bufferSize));

        // only receive data if buffer is larger than 0
        if (bufferSize > 0)
        {
            requests[1] =
                helper::Irecv64(receiveBuffer.Data(), receiveBuffer.m_Position,
                                m_Rank + 1, 1, m_Comm,
                                ", aggregation Irecv64 data at iteration " +
                                    std::to_string(step));
        }
    }

    return requests;
}

std::vector<std::vector<MPI_Request>>
MPIChain::IExchangeAbsolutePosition(format::Buffer &buffer, const int step)
{
    if (m_Size == 1)
    {
        return std::vector<std::vector<MPI_Request>>();
    }

    if (m_IsInExchangeAbsolutePosition)
    {
        throw std::runtime_error("ERROR: MPIChain::IExchangeAbsolutePosition: "
                                 "An existing exchange is still active.");
    }

    const int destination = (step != m_Size - 1) ? step + 1 : 0;
    std::vector<std::vector<MPI_Request>> requests(2,
                                                   std::vector<MPI_Request>(1));

    if (step == 0)
    {
        m_SizeSend =
            (m_Rank == 0) ? buffer.m_AbsolutePosition : buffer.m_Position;
    }

    if (m_Rank == step)
    {
        m_ExchangeAbsolutePosition =
            (m_Rank == 0) ? m_SizeSend : m_SizeSend + buffer.m_AbsolutePosition;

        helper::CheckMPIReturn(
            MPI_Isend(&m_ExchangeAbsolutePosition, 1, ADIOS2_MPI_SIZE_T,
                      destination, 0, m_Comm, &requests[0][0]),
            ", aggregation Isend absolute position at iteration " +
                std::to_string(step) + "\n");
    }
    else if (m_Rank == destination)
    {
        helper::CheckMPIReturn(
            MPI_Irecv(&buffer.m_AbsolutePosition, 1, ADIOS2_MPI_SIZE_T, step, 0,
                      m_Comm, &requests[1][0]),
            ", aggregation Irecv absolute position at iteration " +
                std::to_string(step) + "\n");
    }

    m_IsInExchangeAbsolutePosition = true;
    return requests;
}

void MPIChain::Wait(std::vector<std::vector<MPI_Request>> &requests,
                    const int step)
{
    if (m_Size == 1)
    {
        return;
    }

    const int endRank = m_Size - 1 - step;
    const bool sender = (m_Rank >= 1 && m_Rank <= endRank) ? true : false;
    const bool receiver = (m_Rank < endRank) ? true : false;

    MPI_Status status;
    if (receiver)
    {
        for (auto &req : requests[1])
        {
            helper::CheckMPIReturn(
                MPI_Wait(&req, &status),
                ", aggregation waiting for receiver request at iteration " +
                    std::to_string(step) + "\n");
        }
    }

    if (sender)
    {
        for (auto &req : requests[0])
        {
            helper::CheckMPIReturn(
                MPI_Wait(&req, &status),
                ", aggregation waiting for sender request at iteration " +
                    std::to_string(step) + "\n");
        }
    }
}

void MPIChain::WaitAbsolutePosition(
    std::vector<std::vector<MPI_Request>> &requests, const int step)
{
    if (m_Size == 1)
    {
        return;
    }

    if (!m_IsInExchangeAbsolutePosition)
    {
        throw std::runtime_error("ERROR: MPIChain::WaitAbsolutePosition: An "
                                 "existing exchange is not active.");
    }

    MPI_Status status;
    const int destination = (step != m_Size - 1) ? step + 1 : 0;

    if (m_Rank == destination)
    {
        helper::CheckMPIReturn(
            MPI_Wait(&requests[1][0], &status),
            ", aggregation Irecv Wait absolute position at iteration " +
                std::to_string(step) + "\n");
    }

    if (m_Rank == step)
    {
        helper::CheckMPIReturn(
            MPI_Wait(&requests[0][0], &status),
            ", aggregation Isend Wait absolute position at iteration " +
                std::to_string(step) + "\n");
    }
    m_IsInExchangeAbsolutePosition = false;
}

void MPIChain::SwapBuffers(const int /*step*/) noexcept
{
    m_CurrentBufferOrder = (m_CurrentBufferOrder == 0) ? 1 : 0;
}

void MPIChain::ResetBuffers() noexcept { m_CurrentBufferOrder = 0; }

format::Buffer &MPIChain::GetConsumerBuffer(format::Buffer &buffer)
{
    return GetSender(buffer);
}

// PRIVATE
void MPIChain::HandshakeLinks()
{
    int link = -1;

    MPI_Request sendRequest;
    if (m_Rank > 0) // send
    {
        helper::CheckMPIReturn(
            MPI_Isend(&m_Rank, 1, MPI_INT, m_Rank - 1, 0, m_Comm, &sendRequest),
            "Isend handshake with neighbor, MPIChain aggregator, at Open");
    }

    if (m_Rank < m_Size - 1) // receive
    {
        MPI_Request receiveRequest;
        helper::CheckMPIReturn(
            MPI_Irecv(&link, 1, MPI_INT, m_Rank + 1, 0, m_Comm,
                      &receiveRequest),
            "Irecv handshake with neighbor, MPIChain aggregator, at Open");

        MPI_Status receiveStatus;
        helper::CheckMPIReturn(
            MPI_Wait(&receiveRequest, &receiveStatus),
            "Irecv Wait handshake with neighbor, MPIChain aggregator, at Open");
    }

    if (m_Rank > 0)
    {
        MPI_Status sendStatus;
        helper::CheckMPIReturn(
            MPI_Wait(&sendRequest, &sendStatus),
            "Isend wait handshake with neighbor, MPIChain aggregator, at Open");
    }
}

format::Buffer &MPIChain::GetSender(format::Buffer &buffer)
{
    if (m_CurrentBufferOrder == 0)
    {
        return buffer;
    }
    else
    {
        return *m_Buffers.front();
    }
}

format::Buffer &MPIChain::GetReceiver(format::Buffer &buffer)
{
    if (m_CurrentBufferOrder == 0)
    {
        return *m_Buffers.front();
    }
    else
    {
        return buffer;
    }
}

void MPIChain::ResizeUpdateBuffer(const size_t newSize, format::Buffer &buffer,
                                  const std::string hint)
{
    if (buffer.m_FixedSize > 0)
    {
        if (newSize > buffer.m_FixedSize)
        {
            throw std::invalid_argument(
                "ERROR: requesting new size: " + std::to_string(newSize) +
                " bytes, for fixed size buffer " +
                std::to_string(buffer.m_FixedSize) + " of type " +
                buffer.m_Type + ", allocate more memory\n");
        }
        return; // do nothing if fixed size is enough
    }

    buffer.Resize(newSize, hint);
    buffer.m_Position = newSize;
}

} // end namespace aggregator
} // end namespace adios2
