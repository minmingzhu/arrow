// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "arrow/dataset/scanner.h"
#include <memory>

#include "arrow/compute/context.h"
#include "arrow/dataset/test_util.h"
#include "arrow/record_batch.h"
#include "arrow/testing/generator.h"
#include "arrow/testing/util.h"

namespace arrow {
namespace dataset {

class TestScanner : public DatasetFixtureMixin {
 protected:
  static constexpr int64_t kNumberSources = 2;
  static constexpr int64_t kNumberFragments = 4;
  static constexpr int64_t kNumberBatches = 16;
  static constexpr int64_t kBatchSize = 1024;

  Scanner MakeScanner(std::shared_ptr<RecordBatch> batch) {
    std::vector<std::shared_ptr<RecordBatch>> batches{kNumberBatches, batch};

    DataFragmentVector fragments{kNumberFragments,
                                 std::make_shared<SimpleDataFragment>(batches, options_)};

    DataSourceVector sources{kNumberSources,
                             std::make_shared<SimpleDataSource>(fragments)};

    return Scanner{sources, options_, ctx_};
  }

  void AssertScannerEqualsRepetitionsOf(Scanner scanner,
                                        std::shared_ptr<RecordBatch> batch) {
    const int64_t total_batches = kNumberSources * kNumberBatches * kNumberFragments;
    auto expected = ConstantArrayGenerator::Repeat(total_batches, batch);

    // Verifies that the unified BatchReader is equivalent to flattening all the
    // structures of the scanner, i.e. Scanner[DataSource[ScanTask[RecordBatch]]]
    AssertScannerEquals(expected.get(), &scanner);
  }
};

constexpr int64_t TestScanner::kNumberSources;
constexpr int64_t TestScanner::kNumberFragments;
constexpr int64_t TestScanner::kNumberBatches;
constexpr int64_t TestScanner::kBatchSize;

TEST_F(TestScanner, Scan) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);
  AssertScannerEqualsRepetitionsOf(MakeScanner(batch), batch);
}

TEST_F(TestScanner, FilteredScan) {
  SetSchema({field("f64", float64())});

  double value = 0.5;
  ASSERT_OK_AND_ASSIGN(auto f64,
                       ArrayFromBuilderVisitor(float64(), kBatchSize, kBatchSize / 2,
                                               [&](DoubleBuilder* builder) {
                                                 builder->UnsafeAppend(value);
                                                 builder->UnsafeAppend(-value);
                                                 value += 1.0;
                                               }));

  options_->filter = ("f64"_ > 0.0).Copy();
  options_->evaluator = std::make_shared<TreeEvaluator>();

  auto batch = RecordBatch::Make(schema_, f64->length(), {f64});

  value = 0.5;
  ASSERT_OK_AND_ASSIGN(
      auto f64_filtered,
      ArrayFromBuilderVisitor(float64(), kBatchSize / 2, [&](DoubleBuilder* builder) {
        builder->UnsafeAppend(value);
        value += 1.0;
      }));

  auto filtered_batch =
      RecordBatch::Make(schema_, f64_filtered->length(), {f64_filtered});

  AssertScannerEqualsRepetitionsOf(MakeScanner(batch), filtered_batch);
}

TEST_F(TestScanner, MaterializeMissingColumn) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch_missing_f64 =
      ConstantArrayGenerator::Zeroes(kBatchSize, schema({field("i32", int32())}));

  ASSERT_OK(options_->projector.SetDefaultValue(schema_->GetFieldIndex("f64"),
                                                MakeScalar(2.5)));

  ASSERT_OK_AND_ASSIGN(auto f64, ArrayFromBuilderVisitor(float64(), kBatchSize,
                                                         [&](DoubleBuilder* builder) {
                                                           builder->UnsafeAppend(2.5);
                                                         }));
  auto batch_with_f64 =
      RecordBatch::Make(schema_, f64->length(), {batch_missing_f64->column(0), f64});

  AssertScannerEqualsRepetitionsOf(MakeScanner(batch_missing_f64), batch_with_f64);
}

TEST_F(TestScanner, ToTable) {
  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);
  std::vector<std::shared_ptr<RecordBatch>> batches{
      kNumberBatches * kNumberFragments * kNumberSources, batch};

  std::shared_ptr<Table> expected;
  ASSERT_OK(Table::FromRecordBatches(batches, &expected));

  auto scanner = MakeScanner(batch);
  std::shared_ptr<Table> actual;

  options_->use_threads = false;
  ASSERT_OK_AND_ASSIGN(actual, scanner.ToTable());
  AssertTablesEqual(*expected, *actual);

  // There is no guarantee on the ordering when using multiple threads, but
  // since the RecordBatch is always the same it will pass.
  options_->use_threads = true;
  ASSERT_OK_AND_ASSIGN(actual, scanner.ToTable());
  AssertTablesEqual(*expected, *actual);
}

class TestScannerBuilder : public ::testing::Test {
  void SetUp() {
    DataSourceVector sources;

    schema_ = schema({
        field("b", boolean()),
        field("i8", int8()),
        field("i16", int16()),
        field("i32", int32()),
        field("i64", int64()),
    });

    ASSERT_OK_AND_ASSIGN(dataset_, Dataset::Make(sources, schema_));
  }

 protected:
  ScanContextPtr ctx_;
  std::shared_ptr<Schema> schema_;
  DatasetPtr dataset_;
};

TEST_F(TestScannerBuilder, TestProject) {
  ScannerBuilder builder(dataset_, ctx_);

  // It is valid to request no columns, e.g. `SELECT 1 FROM t WHERE t.a > 0`.
  // still needs to touch the `a` column.
  ASSERT_OK(builder.Project({}));
  ASSERT_OK(builder.Project({"i64", "b", "i8"}));
  ASSERT_OK(builder.Project({"i16", "i16"}));

  ASSERT_RAISES(Invalid, builder.Project({"not_found_column"}));
  ASSERT_RAISES(Invalid, builder.Project({"i8", "not_found_column"}));
}

TEST_F(TestScannerBuilder, TestFilter) {
  ScannerBuilder builder(dataset_, ctx_);

  ASSERT_OK(builder.Filter(scalar(true)));
  ASSERT_OK(builder.Filter("i64"_ == int64_t(10)));
  ASSERT_OK(builder.Filter("i64"_ == int64_t(10) || "b"_ == true));

  ASSERT_RAISES(TypeError, builder.Filter("i64"_ == int32_t(10)));
  ASSERT_RAISES(Invalid, builder.Filter("not_a_column"_ == true));
  ASSERT_RAISES(Invalid,
                builder.Filter("i64"_ == int64_t(10) || "not_a_column"_ == true));
}

}  // namespace dataset
}  // namespace arrow
