* Finish litestl::util::Vector bindings
  - Create a system to find vector template instantiations in the
    binding manager for a given type
  - Automatically create and free vectors when passing them (as references or pointers)
    to methods or constructors
