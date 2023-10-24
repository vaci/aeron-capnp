@0xc3b7dc8e9fe41740;

$import "/capnp/c++.capnp".namespace("aeron");

struct Syn {
  channel @0 :Text;
  streamId @1 :Int32;
}

struct Ack {
  sessionId @0 :Int32;
}
