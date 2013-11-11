// #include <string.h>
// #include <unistd.h>
// #include <errno.h>
#include "async_file.h"
#include "logging.h"

async_file::async_file(boost::asio::io_service& service)
    : sd(service)
    , outqd(0)
    , st(Ok)
    , endread(false)
{
}

async_file::~async_file()
{
    close();
}

bool async_file::open(OpenMode)
{
    logger::fatal() << "Do not call async_file::open(OpenMode).";
    return false;
}

bool async_file::open(int fd, OpenMode mode)
{
    sd.assign(::dup(fd));

    logger::debug() << "Open fd " << fd << " mode " << mode;
    assert(mode & ReadWrite);

    if (sd.is_open()) {
        set_error("async_file already open");
        logger::debug() << error_string();
        return false;
    }

    // Place the file descriptor in nonblocking mode
    sd.non_blocking(true);

    mode_ = mode;

    if (mode & Read) {
        read_some();
    }
    if (mode & Write) {
    //     snout = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    //     connect(snout, SIGNAL(activated(int)),
    //         this, SLOT(readyWrite()));
    }

    return true;
}

void async_file::close_read()
{
    endread = true;
    mode_ &= ~Read;
}

void async_file::read_some(
    boost::system::error_code const& error, // Result of operation.
    std::size_t bytes_transferred)          // Number of bytes read.
{
    sd.async_read_some(boost::asio::buffer(data, size), [this]{read_some();});
}

ssize_t async_file::read_data(char *data, ssize_t maxSize)
{
    assert(sd.is_open());
    assert(mode_ & Read);

    ssize_t act = ::read(fd, data, maxSize);
    if (act < 0) {
        if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
        {
            set_error(strerror(errno));
            logger::debug() << error_string();
            return -1;  // a real error occurred
        }
        else
            return 0; // nothing serious
    }
    if (act == 0) {
        endread = true;
    }
    return act;
}

bool async_file::at_end() const
{
    return endread;
}

ssize_t async_file::write_data(const char *data, ssize_t maxSize)
{
    assert(sd.is_open());
    assert(mode_ & Write);

    // Write the data immediately if possible
    ssize_t act = 0;
    if (outq.empty()) {
        act = ::write(fd, data, maxSize);
        if (act < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                set_error(strerror(errno));
                logger::debug() << error_string();
                return -1;  // a real error occurred
            }
            act = 0;    // nothing serious
        }
        data += act;
        maxSize -= act;
    }

    // Buffer whatever we can't write.
    if (maxSize > 0) {
        outq.emplace_back(data, maxSize);
        outqd += maxSize;
        act += maxSize;
    }

    return act;
}

void async_file::ready_write()
{
    while (!outq.empty()) {
        byte_array& buf = outq.front();
        ssize_t act = ::write(fd, buf.data(), buf.size());
        if (act < 0) {
            if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
            {
                // A real error: empty the output buffer
                set_error(strerror(errno));
                logger::debug() << error_string();
                outq.clear();
                return;
            }
            act = 0;    // nothing serious
        }

        if (act < buf.size()) {
            // Partial write: leave the rest buffered
            buf = buf.mid(act);
            return;
        }

        // Full write: proceed to the next buffer
        outq.pop_front();
    }
}

void async_file::set_error(std::string const& msg)
{
    st = Error;
    setErrorString(msg);
}

void async_file::close()
{
    sd.close();
}

// Public API
/**
 * Check the input queue and return some data if available.
 * @param  buf      [description]
 * @param  max_size [description]
 * @return          [description]
 */
ssize_t async_file::read(char* buf, ssize_t max_size)
{}

byte_array async_file::read(ssize_t max_size)
{}

ssize_t async_file::write(char const* buf, ssize_t size)
{}

ssize_t async_file::write(byte_array const& buf)
{}
