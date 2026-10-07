#pragma once

#include <memory>
#include <string>

class HttpTestServer
{
public:
    HttpTestServer();
    ~HttpTestServer();

    std::string Url() const;
    void Stop();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
