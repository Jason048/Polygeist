// RUN: polygeist-opt --mem2reg --split-input-file %s | FileCheck %s

module {
  func.func @f() -> f64 {
    %cst = arith.constant 3.141592e+00 : f64
    %c0 = arith.constant 0 : index
    %0 = memref.alloca() : memref<f64, 5>
    %1 = llvm.mlir.undef : f64
    memref.store %1, %0[] : memref<f64, 5>
      %8 = gpu.thread_id  x
      %10 = arith.cmpi eq, %8, %c0 : index
      scf.if %10 {
        memref.store %cst, %0[] : memref<f64, 5>
      }
      nvvm.barrier0
      %r11 = memref.load %0[] : memref<f64, 5>
    return %r11 : f64
  }
}

// CHECK:  func.func @f() -> f64 {
// CHECK-DAG:    %cst = arith.constant 3.1415920000000002 : f64
// CHECK-DAG:    %c0 = arith.constant 0 : index
// CHECK-NEXT:    %0 = memref.alloca() : memref<f64, 5>
// CHECK-NEXT:    %1 = llvm.mlir.undef : f64
// CHECK-NEXT:    memref.store %1, %0[] : memref<f64, 5>
// CHECK-NEXT:    %2 = gpu.thread_id  x
// CHECK-NEXT:    %3 = arith.cmpi eq, %2, %c0 : index
// CHECK-NEXT:    scf.if %3 {
// CHECK-NEXT:      memref.store %cst, %0[] : memref<f64, 5>
// CHECK-NEXT:    }
// CHECK-NEXT:    nvvm.barrier0
// CHECK-NEXT:    %4 = memref.load %0[] : memref<f64, 5>
// CHECK-NEXT:    return %4 : f64
// CHECK-NEXT:  }
