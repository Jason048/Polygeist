//===- Polygeistps.h - Polygeist dialect ops --------------------*- C++ -*-===//
//
// This file is licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef POLYGEISTOPS_H
#define POLYGEISTOPS_H

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Dialect.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Interfaces/ViewLikeInterface.h"
#include "llvm/Support/CommandLine.h"

#define GET_OP_CLASSES
#include "polygeist/PolygeistOps.h.inc"

#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/SCF/IR/SCF.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/PatternMatch.h"
#include "llvm/Support/CommandLine.h"

bool collectEffects(
    mlir::Operation *op,
    llvm::SmallVectorImpl<mlir::MemoryEffects::EffectInstance> &effects,
    bool ignoreBarriers);

bool getEffectsBefore(
    mlir::Operation *op,
    llvm::SmallVectorImpl<mlir::MemoryEffects::EffectInstance> &effects,
    bool stopAtBarrier);

bool getEffectsAfter(
    mlir::Operation *op,
    llvm::SmallVectorImpl<mlir::MemoryEffects::EffectInstance> &effects,
    bool stopAtBarrier);

bool isReadOnly(mlir::Operation *);
bool isReadNone(mlir::Operation *);

bool mayReadFrom(mlir::Operation *, mlir::Value);
bool mayWriteTo(mlir::Operation *, mlir::Value, bool ignoreBarrier = false);

bool mayAlias(mlir::MemoryEffects::EffectInstance a,
              mlir::MemoryEffects::EffectInstance b);

bool mayAlias(mlir::MemoryEffects::EffectInstance a, mlir::Value b);

extern llvm::cl::opt<bool> BarrierOpt;

template <bool NotTopLevel = false>
class BarrierElim final
    : public mlir::OpRewritePattern<mlir::polygeist::BarrierOp> {
public:
  using mlir::OpRewritePattern<mlir::polygeist::BarrierOp>::OpRewritePattern;
  mlir::LogicalResult
  matchAndRewrite(mlir::polygeist::BarrierOp barrier,
                  mlir::PatternRewriter &rewriter) const override {
    using namespace mlir;
    using namespace polygeist;
    if (!BarrierOpt)
      return failure();
    // Remove if it only sync's constant indices.
    if (llvm::all_of(barrier.getOperands(), [](mlir::Value v) {
          IntegerAttr constValue;
          return matchPattern(v, m_Constant(&constValue));
        })) {
      rewriter.eraseOp(barrier);
      return success();
    }

    Operation *op = barrier;
    if (NotTopLevel && isa<mlir::scf::ParallelOp, mlir::AffineParallelOp>(
                           barrier->getParentOp()))
      return failure();

    {
      SmallVector<MemoryEffects::EffectInstance> beforeEffects;
      getEffectsBefore(op, beforeEffects, /*stopAtBarrier*/ true);

      SmallVector<MemoryEffects::EffectInstance> afterEffects;
      getEffectsAfter(op, afterEffects, /*stopAtBarrier*/ false);

      bool conflict = false;
      for (auto before : beforeEffects)
        for (auto after : afterEffects) {
          if (mayAlias(before, after)) {
            // Read, read is okay
            if (isa<MemoryEffects::Read>(before.getEffect()) &&
                isa<MemoryEffects::Read>(after.getEffect())) {
              continue;
            }

            // Write, write is not okay because may be different offsets and the
            // later must subsume other conflicts are invalid.
            conflict = true;
            break;
          }
        }

      if (!conflict) {
        rewriter.eraseOp(barrier);
        return success();
      }
    }

    {
      SmallVector<MemoryEffects::EffectInstance> beforeEffects;
      getEffectsBefore(op, beforeEffects, /*stopAtBarrier*/ false);

      SmallVector<MemoryEffects::EffectInstance> afterEffects;
      getEffectsAfter(op, afterEffects, /*stopAtBarrier*/ true);

      bool conflict = false;
      for (auto before : beforeEffects)
        for (auto after : afterEffects) {
          if (mayAlias(before, after)) {
            // Read, read is okay
            if (isa<MemoryEffects::Read>(before.getEffect()) &&
                isa<MemoryEffects::Read>(after.getEffect())) {
              continue;
            }
            // Write, write is not okay because may be different offsets and the
            // later must subsume other conflicts are invalid.
            conflict = true;
            break;
          }
        }

      if (!conflict) {
        rewriter.eraseOp(barrier);
        return success();
      }
    }

    return failure();
  }
};

struct ValueOrInt {
  bool isValue;
  mlir::Value v_val;
  int64_t i_val;
  ValueOrInt(mlir::Value v) { initValue(v); }
  void initValue(mlir::Value v) {
    using namespace mlir;
    if (v) {
      IntegerAttr iattr;
      if (matchPattern(v, m_Constant(&iattr))) {
        i_val = iattr.getValue().getSExtValue();
        v_val = nullptr;
        isValue = false;
        return;
      }
    }
    isValue = true;
    v_val = v;
  }

  ValueOrInt(size_t i) : isValue(false), v_val(), i_val(i) {}

  bool operator>=(int64_t v) {
    if (isValue)
      return false;
    return i_val >= v;
  }
  bool operator>(int64_t v) {
    if (isValue)
      return false;
    return i_val > v;
  }
  bool operator==(int64_t v) {
    if (isValue)
      return false;
    return i_val == v;
  }
  bool operator<(int64_t v) {
    if (isValue)
      return false;
    return i_val < v;
  }
  bool operator<=(int64_t v) {
    if (isValue)
      return false;
    return i_val <= v;
  }
  bool operator>=(llvm::APInt v) {
    if (isValue)
      return false;
    return i_val >= v.getSExtValue();
  }
  bool operator>(llvm::APInt v) {
    if (isValue)
      return false;
    return i_val > v.getSExtValue();
  }
  bool operator==(llvm::APInt v) {
    if (isValue)
      return false;
    return i_val == v.getSExtValue();
  }
  bool operator<(llvm::APInt v) {
    if (isValue)
      return false;
    return i_val < v.getSExtValue();
  }
  bool operator<=(llvm::APInt v) {
    if (isValue)
      return false;
    return i_val <= v.getSExtValue();
  }
};

enum class Cmp { EQ, LT, LE, GT, GE };

bool valueCmp(Cmp cmp, mlir::AffineExpr expr, size_t numDim,
              mlir::ValueRange operands, ValueOrInt val);

bool valueCmp(Cmp cmp, mlir::Value bval, ValueOrInt val);
#endif // POLYGEISTOPS_H
