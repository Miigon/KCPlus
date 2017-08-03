/*
    Copyright 2017 Miigon

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <functional>
#include <memory>
#include <stdexcept>
#include <ikcp.h>

namespace ikcp
{
    using IUINT32 = IUINT32;
    using SizeType = std::size_t;

    /**
     * @brief High-level packet returned by `KCPSession::receive()`.
     * @details
     * `data` field is `nullptr` and `size` field is `0` if there is no high-level packet available.
     * @see KCPSession::receive()
     */
    struct Packet
    {
        std::unique_ptr<char [], std::default_delete<char []>> data;
        SizeType size;
    };

    /**
     * @brief A KCP session for sending/receiving packets.
     */
    class KCPSession
    {
    public:
        using Timestamp = IUINT32;
        using OutputFunction = std::function<void(const char buf[], SizeType len)>;
        using receiveCallback = std::function<void(Packet packet)>;
        /**
         * @param conv
         * The connection identifier.
         * Must be equal between two endpoint KCPSession.
         */
        KCPSession(IUINT32 conv = 0)
            :mKcp(ikcp_create(conv,this)),mOutputFunc(nullptr)
        {
            mKcp->output = mOutputFuncRaw;
        };

        ~KCPSession()
        {
            ikcp_release(mKcp);
        }

        /**
         * @brief Turns on/off async mode. (KCPlus feature)
         * @details
         * Async mode: When a new packet arrived, callback set by `setReceiveCallback()` will be called.
         * You don't need to call `receive()` to receive packets.
         * You still need to call `update()` to provide timestamps and update state.
         * @param AsyncMode Async mode on/off
         */
        void setAsyncMode(bool AsyncMode)
        {
            mAsyncMode = AsyncMode;
        }

        /**
         * @brief Set packet receiving function under async mode.
         * @param receiveCallback The callback function called when receiving new packet under async mode.
         */
        void setReceiveCallback(receiveCallback receiveCallback)
        {
            mReceiveFunc = receiveCallback;
        }

        /**
         * @brief Sets the callback function for sending a low-level packet to the remote.
         * @param outputFunction The callback function for sending a low-level packet to the remote.
         */
        void setOutputFunction(OutputFunction outputFunction)
        {
            mOutputFunc = outputFunction;
        }

        /**
         * @brief If you received a low-level packet, call this function.
         * @param data Data of the low-level packet.
         * @param size Size of data.
         */
        void input(const char data[], SizeType size)
        {
            ikcp_input(mKcp, data, size);
            if(mAsyncMode && hasReceivablePacket())
            {
                mReceiveFunc(std::move(receive()));
            }
        }

        /**
         * @brief Returns whether there is a new packet to receive(`receive()`) or not.
         *
         * @return If there is a new packet to receive, returns `true`.
         */
        bool hasReceivablePacket() const
        {
            return ikcp_peeksize(mKcp) >= 0;
        }

        /**
         * @brief Returns the size of next packet.
         * @return If there is a new packet to receive, returns it's size. Otherwise returns 0.
         * @note
         * This function CANNOT determine whether a new packet exists or not, because size of a regular packet can be
         * 0 too. Use `hasReceivablePacket()` to do that.
         */
        SizeType nextPacketSize() const
        {
            int size = ikcp_peeksize(mKcp);
            return size >= 0 ? static_cast<SizeType>(size) : 0;
        }

        /**
         * @brief Receives a high-level packet.
         * @return A high-level packet, `data` field is `nullptr` and `size` field is `0` if there is no high-level
         * packet available. You can detect whether there is a new packet to receive or not by calling
         * `hasReceivablePacket()`.
         * @see Packet
         * @see hasReceivablePacket()
         */
        Packet receive()
        {
            Packet packet{nullptr, 0};
            if(hasReceivablePacket())
            {
                SizeType packetSize = nextPacketSize();
#if __cplusplus == 201402L
                packet.data = std::make_unique<char []>(packetSize);
#else
                packet.data = std::unique_ptr<char []>(new char[packetSize]);
#endif
                packet.size = packetSize;
                if(ikcp_recv(mKcp, packet.data.get(), static_cast<int>(packetSize)) != packetSize)
                {
                    throw std::runtime_error("receive: Internal inconsistency error!");
                    // Internal inconsistency error! `ikcp_recv()` should equal `ikcp_peeksize()` because they return
                    // size of the same packet.
                    // You DON'T need to catch this exception because it is generally a bug. Eg. multi-thread bug.
                }
            }
            return std::move(packet);
        }

        /**
         * @brief Sends a high-level packet to the remote.
         * @details
         * @note The actual low-level packets will be pending, instead of sending immediately.
         * Call `flush()` to send them immediately, or wait for next `update()` call.
         * @param data Data to be sent.
         * @param size Size of data.
         *
         * @see update()
         * @see flush()
         */
        void send(char data[], SizeType size)
        {
            ikcp_send(mKcp, data, static_cast<int>(size));
        }

        /**
         * @brief Updates session state.
         * @details
         * Call it repeatedly, every 10ms-100ms. You can also ask `whenToUpdate()` for when to call it again.
         * Low-level packets will be sent in this function(by calling `flush()`). Timestamp will be updated as well.
         * @param currentTimestamp Current Timestamp.
         *
         * @see whenToUpdate()
         * @see send()
         * @see flush()
         */
        void update(IUINT32 currentTimestamp)
        {
            ikcp_update(mKcp, currentTimestamp);
        }

        /**
         * @brief Send out (and flush) all pending low-level packets.
         * @see send()
         * @see update()
         */
        void flush()
        {
            ikcp_flush(mKcp);
        }

        /**
         * @brief Determines when you should invoke `update()` for the next time.
         * After that time you should invoke `update()` to update session state and flush data.
         * This was designed for optimizing.
         * @param currentTimestamp Current unix timestamp.
         * @return Timestamp of the time you should invoke `update()`.
         * @see update()
         */
        IUINT32 whenToUpdate(IUINT32 currentTimestamp)
        {
            return ikcp_check(mKcp, currentTimestamp);
        }

        /**
         * @brief Sets the MTU (Maximum Transmission Unit).
         * @details
         * By default it's 1400.
         * Packets larger than MTU will be sliced into several packets by KCP.
         * Better be equal or smaller than the ethernet MTU.
         * @param mtu New MTU value.
         */
        void setMTU(int mtu)
        {
            ikcp_setmtu(mKcp, mtu);
        }

        /**
         * @brief Sets maximum send window size.
         * @note Too low send window size can easily increase blocking probability.
         * @param sendWindow New maximum send window size.
         */
        void setMaxSendWindowSize(int sendWindow)
        {
            ikcp_wndsize(mKcp, sendWindow, 0);
        }

        /**
         * @brief Sets maximum receive window size.
         * @note Too low send window size can easily increase congestion probability.
         * @param receiveWindow New maximum receive window size.
         */
        void setMaxReceiveWindowSize(int receiveWindow)
        {
            ikcp_wndsize(mKcp, 0, receiveWindow);
        }

        /**
         * @brief Returns how many pending packets to be sent.
         * @return How many pending packets to be sent.
         */
        SizeType getNumOfPendingPackets() const
        {
            return static_cast<SizeType>(ikcp_waitsnd(mKcp));
        }

        /**
         * @brief Sets KCP properties.
         * @details This is like a macro of 4 functions below.
         * Fastest: true, 20, 2, true
         * @param nodelay See `setNodelay()`
         * @param internalInterval  See `setInternalInterval()`
         * @param fastResendThreshold See `setFastResendThreshold()`
         * @param congestionControl See `setCongestionControl()`
         */
        void setProperties(bool nodelay, int internalInterval, int fastResendThreshold, bool congestionControl)
        {
            setNodelay(nodelay);
            setInternalInterval(internalInterval);
            setFastResendThreshold(fastResendThreshold);
            setCongestionControl(congestionControl);
        }

        /**
         * @brief Turns on/off nodelay mode.
         * @param nodelay Nodelay mode on/off.
         */
        void setNodelay(bool nodelay)
        {
            setPropertiesPrivate(nodelay ? 1 : 0, NotChanged, NotChanged, NotChanged);
        }

        /**
         * @brief Sets the interval of internal update timer.
         * @param internalInterval Interval in millisec, default: 100ms.
         */
        void setInternalInterval(int internalInterval)
        {
            setPropertiesPrivate(NotChanged, internalInterval, NotChanged, NotChanged);
        }

        /**
         * @brief Sets the threshold of fast resend.
         * @param fastResendThreshold Threshold of fast resend.
         * If the frequency of receiving ack packets of the data packets after a particular packet, fast resend will be triggered.
         */
        void setFastResendThreshold(int fastResendThreshold)
        {
            setPropertiesPrivate(NotChanged, NotChanged, fastResendThreshold, NotChanged);
        }

        /**
         * @brief Turns on/off the congestion control.
         * @param congestionControl Congestion control on/off.
         */
        void setCongestionControl(bool congestionControl)
        {
            setPropertiesPrivate(NotChanged, NotChanged, NotChanged, congestionControl ? 0 : 1);
        }

        void log(int mask, const char format[], ...)
        {
            va_list argptr;
            va_start(argptr, format);
            ikcp_log(mKcp, mask, format, argptr);
            va_end(argptr);
        }

        /**
         * @brief Sets allocator and deallocator for internal buffer allocating.
         * @param allocator Allocator function.
         * @param deallocator Deallocator function.
         */
        static void setAllocator(void* (*allocator)(size_t), void (*deallocator)(void*))
        {
            ikcp_allocator(allocator,deallocator);
        }
    private:
        ikcpcb *mKcp;
        OutputFunction mOutputFunc;
        bool mAsyncMode;
        receiveCallback mReceiveFunc;


        static int mOutputFuncRaw(const char buf[], int len, ikcpcb *kcp, void *user)
        {
            auto thisptr = reinterpret_cast<KCPSession *>(user);
            assert(thisptr->mOutputFunc != nullptr);
            thisptr->mOutputFunc(buf, static_cast<SizeType>(len));
            return 0;
        }

        void setPropertiesPrivate(int nodelay, int interval, int resend, int nc)
        {
            ikcp_nodelay(mKcp, nodelay, interval, resend, nc);
        }

        constexpr static const int NotChanged = -1;
    };
}
