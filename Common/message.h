#pragma once

namespace c_rpc {

struct message {
    unsigned char magic;
    unsigned char version;
    unsigned char type;
    unsigned char reserved;
    unsigned int sequence_id;
    unsigned int content_length;
    char content[0];
};

}