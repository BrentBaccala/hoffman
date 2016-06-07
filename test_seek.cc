#include <fstream>
#include <boost/iostreams/filtering_stream.hpp>

namespace io = boost::iostreams;

main()
{
    std::ifstream input_file("hi");
    //io::filtering_istream instream;
    io::filtering_stream<io::input_seekable> instream;

    instream.push(input_file);

    input_file.exceptions(std::ifstream::failbit | std::ifstream::badbit);
    instream.exceptions(std::ifstream::failbit | std::ifstream::badbit);

    // this works
    input_file.seekg(0);

    // this doesn't
    instream.seekg(0);
}
