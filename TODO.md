* Refactor typescript.cc from a couple of utility functions and one giant function with lots of lambdas into
  a single C++ class.  Each utility function and lambda would become a method on that class.  Captured
  variables (other than ones that turned into methods) would become class properties.  
  generateTypescript itself would become a "generate" method.
  This will be needed when implementing discriminated unions, to
  avoid a circular reference issue between formatType and formatTemplate.
