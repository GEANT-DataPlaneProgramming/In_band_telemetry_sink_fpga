#include <boost/asio.hpp>
#include <chrono>
#include <string>

#ifndef INT_TRANSPORTS_UDP_H
#define INT_TRANSPORTS_UDP_H

/// \brief UDP transport
class INT_UDP
{
    public:
        /// Constructor
        INT_UDP(const std::string &hostname, int port);

        /// Sends blob via UDP
        void send(std::string& message);

    private:
        /// Boost Asio I/O functionality
        boost::asio::io_service mIoService;

        /// UDP socket
        boost::asio::ip::udp::socket mSocket;

        /// UDP endpoint
        boost::asio::ip::udp::endpoint mEndpoint;
};

#endif // INFLUXDATA_TRANSPORTS_UDP_H
