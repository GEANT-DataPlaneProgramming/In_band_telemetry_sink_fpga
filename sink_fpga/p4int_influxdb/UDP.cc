#include "UDP.h"
#include <string>

INT_UDP::INT_UDP(const std::string &hostname, int port) :
    mSocket(mIoService, boost::asio::ip::udp::endpoint(boost::asio::ip::udp::v4(), 0))
{
    boost::asio::ip::udp::resolver resolver(mIoService);
    boost::asio::ip::udp::resolver::query query(boost::asio::ip::udp::v4(), hostname, std::to_string(port));
    boost::asio::ip::udp::resolver::iterator resolverInerator = resolver.resolve(query);
    mEndpoint = *resolverInerator;
}

void INT_UDP::send(std::string &message)
{
    try {
        mSocket.send_to(boost::asio::buffer(message, message.size()), mEndpoint);
    }
    catch (const boost::system::system_error &e)                                                                                                                                                                                                                                      {
        
        fprintf(stderr, "%s, %lu\n", e.what(), message.size());
    }
}
