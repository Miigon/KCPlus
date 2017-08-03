# KCPlus
Header-only, lightweight and easy to use [KCP][1] wrapper in C++ 11/14.  
[![Powered][2]][1]  

# Contributing
You can contribute by making pull requests, raising issues, reporting bugs or making documentation better.

# Guide

## Installation
1. Make sure you have [KCP][1] installed.  
2. Simply include `kcplus.hpp` in your project.  

## Documentations
KCPlus is documented with doxygen. The config file is `doxygen.cfg`.  
Execute the following command:  
```
doxygen doxygen.cfg
```
And you can found documentations under `./html` directory.  

## Examples
### HelloWorld
```c++
int main()
{
    ikcp::KCPSession user1;
    ikcp::KCPSession user2;
    user1.setOutputFunction([&user2](const char data[],ikcp::SizeType size)
    {
        // In this example, we transfer data to user2 directly.
        // You can use UDP to transfer data too. Just send data as UDP packet.
        user2.input(data,size);
    });
    user2.setOutputFunction([&user1](const char data[],ikcp::SizeType size)
    {
        // The same as above.
        user1.input(data,size);
    });

    // Set up a interval timer for updating state
    // Replace these codes with actual timer creating codes.
    setUpIntervalTimer(20ms,[]()
    {
        user1.update(std::time(nullptr));
        user2.update(std::time(nullptr));
    }]);
    
    user1.send("Hello,world!",12); // Will NOT send low-level packets(by output function) immediately.
    user1.flush(); // Send low-level packets to user2.
    if(user2.hasReceivablePacket())
    {
        ikcp::Packet packet = user2.receive(); // Receive packet sent by user1.
        std::cout.write(packet.data.get(),packet.size);
    }
}
```

### C/S and Async mode
```c++
int main()
{
    // Server side needs to create KCPSessions for every client connection.
    ikcp::KCPSession serverside_client1;
    ikcp::KCPSession serverside_client2;
    // Client side session.
    ikcp::KCPSession client1;
    ikcp::KCPSession client2;
    client1.setOutputFunction([&serverside_client1](const char data[],ikcp::SizeType size)
    {
        serverside_client1.input(data,size);
    });
    client2.setOutputFunction([&serverside_client2](const char data[],ikcp::SizeType size)
    {
        serverside_client2.input(data,size);
    });

    setUpIntervalTimer(20ms,[]()
    {
        client1.update(std::time(nullptr));
        client2.update(std::time(nullptr));
        serverside_client1.update(std::time(nullptr));
        serverside_client2.update(std::time(nullptr));
    }]);

    // Enable async mode for client2 session.
    // For more information about async mode, check out `ikcp::KCPSession::setAsyncMode()` in documentation.
    serverside_client2.setAsyncMode(true);
    serverside_client2.setReceiveCallback([](ikcp::Packet packet)
    {
        std::cout << "From client2: ";
        std::cout.write(packet.data.get(),packet.size);
        std::cout << std::endl;
    });

    client1.send("Hello,server?",12);
    client1.flush();
    client2.send("I am client 2!",14);
    client2.flush();
    if(serverside_client1.hasReceivablePacket())
    {
        ikcp::Packet packet = serverside_client1.receive(); // Receive packet sent by client1.
        std::cout << "From client1: ";
        std::cout.write(packet.data.get(),packet.size);
        std::cout << std::endl;
    }
    // Packet sent by client2 was automatically received by async mode feature.
}
```

[1]: https://github.com/skywind3000/kcp
[2]: http://skywind3000.github.io/word/images/kcp.svg
