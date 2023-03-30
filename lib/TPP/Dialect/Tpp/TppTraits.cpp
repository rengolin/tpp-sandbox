#include "TPP/Dialect/Tpp/TppTraits.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/Traits.h"

using namespace mlir;
using namespace mlir::OpTrait;
using namespace mlir::OpTrait::tpp;

static ArrayRef<int64_t> getShape(Type type) {
  if (auto sType = type.dyn_cast<ShapedType>())
    return sType.getShape();
  return {};
}

static bool isCompatibleInferredReturnShape(ArrayRef<int64_t> inferred,
                                            ArrayRef<int64_t> existing) {
  auto isCompatible = [](int64_t dim1, int64_t dim2) {
    // If the inferred and existing dim is the same, or one of them is unknown
    // then it is compatible, else if the inferred dim is 1 then it is also
    // compatible. But if the existing dim is 1 and the inferred is greater than
    // 1 then flag.
    return dim1 == dim2 || ShapedType::isDynamic(dim1) ||
           ShapedType::isDynamic(dim2) || dim1 == 1;
  };
  if (inferred.size() != existing.size())
    return false;
  for (auto p : llvm::zip_equal(inferred, existing))
    if (!isCompatible(std::get<0>(p), std::get<1>(p)))
      return false;
  return true;
}

static LogicalResult verifyCompatibleOperandBroadcast(Operation *op,
                                                      TypeRange inputTypes,
                                                      Type outputType,
                                                      bool emitDiagnostic) {
  // No input nothing to verify.
  if (inputTypes.empty())
    return success();

  SmallVector<int64_t> resultShape;
  (void)OpTrait::util::getBroadcastedShape(getShape(inputTypes.front()), {},
                                           resultShape);
  for (auto other : llvm::make_early_inc_range(inputTypes)) {
    SmallVector<int64_t> temp = resultShape;
    if (!OpTrait::util::getBroadcastedShape(temp, getShape(other),
                                            resultShape)) {
      if (emitDiagnostic)
        return op->emitOpError(
            "operands don't have broadcast-compatible shapes");
      return failure();
    }
  }

  ArrayRef<int64_t> actualSuffix =
      getShape(outputType).take_back(resultShape.size());
  if (!isCompatibleInferredReturnShape(resultShape, actualSuffix)) {
    if (emitDiagnostic) {
      return op->emitOpError() << "result type not broadcast compatible with "
                                  "broadcasted operands's shapes";
    }
    return failure();
  }
  return success();
}

LogicalResult
mlir::OpTrait::tpp::verifyBroadcastableShape(Operation *op,
                                             bool emitDiagnostic) {
  TypeRange operandTypes = op->getOperandTypes();

  // Get input operands all but last.
  SmallVector<Type> inputOperandTypes;
  for (size_t idx = 0, end = operandTypes.size() - 1; idx < end; idx++) {
    inputOperandTypes.push_back(operandTypes[idx]);
  }
  return verifyCompatibleOperandBroadcast(op, inputOperandTypes,
                                          operandTypes[operandTypes.size() - 1],
                                          emitDiagnostic);
}

// Verify all the operands have stride one in the fastest-varying dimension.
LogicalResult
mlir::OpTrait::tpp::verifyUnitStrideInnerLoop(Operation *op,
                                              bool emitDiagnostic) {
  SmallVector<int64_t> strides;
  int64_t offset;
  for (auto [idx, operand] : llvm::enumerate(op->getOperands())) {
    auto operandType = operand.getType();
    // Non-shaped type return success.
    if (!isa<ShapedType>(operandType))
      return success();
    if (failed(getStridesAndOffset(operandType.cast<MemRefType>(), strides,
                                   offset))) {
      if (emitDiagnostic)
        return op->emitError()
               << "failed to compute strides for operand " << idx;
      return failure();
    }

    // For 0-rank memref `getStridesAndOffset` does not fail and return 0
    // strides.
    if (strides.empty())
      return failure();

    assert(!strides.empty());
    if (strides.back() != 1) {
      if (emitDiagnostic) {
        return op->emitError() << "non-unit stride in the innermost varying "
                                  "dimension for operand "
                               << idx;
      }
      return failure();
    }
  }
  return success();
}

LogicalResult mlir::OpTrait::tpp::checkBroadcastableShape(Operation *op) {
  return verifyBroadcastableShape(op, /*emitDiagnostic=*/false);
}

LogicalResult mlir::OpTrait::tpp::checkUnitStrideInnerLoop(Operation *op) {
  return verifyUnitStrideInnerLoop(op, /*emitDiagnostic=*/false);
}