
#pragma once

union ibv_gid;

void wire_gid_to_gid(const char* wgid,union ibv_gid* gid);

void gid_to_wire_gid(const union ibv_gid* gid,char wgid[]);

static inline unsigned long
align_any(unsigned long val, unsigned long algn)
{
    return val + ((val % algn) ? (algn - (val % algn)) : 0);
}