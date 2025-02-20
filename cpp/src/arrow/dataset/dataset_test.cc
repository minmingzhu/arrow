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

#include "arrow/dataset/dataset.h"

#include <tuple>

#include "arrow/dataset/dataset_internal.h"
#include "arrow/dataset/discovery.h"
#include "arrow/dataset/partition.h"
#include "arrow/dataset/test_util.h"
#include "arrow/filesystem/mockfs.h"
#include "arrow/stl.h"
#include "arrow/testing/generator.h"
#include "arrow/util/optional.h"

namespace arrow {
namespace dataset {

class TestSimpleDataFragment : public DatasetFixtureMixin {};

TEST_F(TestSimpleDataFragment, Scan) {
  constexpr int64_t kBatchSize = 1024;
  constexpr int64_t kNumberBatches = 16;

  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);
  auto reader = ConstantArrayGenerator::Repeat(kNumberBatches, batch);

  // Creates a SimpleDataFragment of the same repeated batch.
  auto fragment = SimpleDataFragment({kNumberBatches, batch}, options_);

  AssertFragmentEquals(reader.get(), &fragment);
}

class TestSimpleDataSource : public DatasetFixtureMixin {};

TEST_F(TestSimpleDataSource, GetFragments) {
  constexpr int64_t kNumberFragments = 4;
  constexpr int64_t kBatchSize = 1024;
  constexpr int64_t kNumberBatches = 16;

  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);
  auto reader = ConstantArrayGenerator::Repeat(kNumberBatches * kNumberFragments, batch);

  std::vector<std::shared_ptr<RecordBatch>> batches{kNumberBatches, batch};
  auto fragment = std::make_shared<SimpleDataFragment>(batches, options_);
  // It is safe to copy fragment multiple time since Scan() does not consume
  // the internal array.
  auto source = SimpleDataSource({kNumberFragments, fragment});

  AssertDataSourceEquals(reader.get(), &source);
}

class TestTreeDataSource : public DatasetFixtureMixin {};

TEST_F(TestTreeDataSource, GetFragments) {
  constexpr int64_t kBatchSize = 1024;
  constexpr int64_t kNumberBatches = 16;
  constexpr int64_t kChildPerNode = 2;
  constexpr int64_t kCompleteBinaryTreeDepth = 4;

  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);

  auto n_leaves = 1U << kCompleteBinaryTreeDepth;
  auto reader = ConstantArrayGenerator::Repeat(kNumberBatches * n_leaves, batch);

  std::vector<std::shared_ptr<RecordBatch>> batches{kNumberBatches, batch};
  auto fragment = std::make_shared<SimpleDataFragment>(batches, options_);

  // Creates a complete binary tree of depth kCompleteBinaryTreeDepth where the
  // leaves are SimpleDataSource containing kChildPerNode fragments.

  auto l1_leaf_source =
      std::make_shared<SimpleDataSource>(DataFragmentVector{kChildPerNode, fragment});

  auto l2_leaf_tree_source =
      std::make_shared<TreeDataSource>(DataSourceVector{kChildPerNode, l1_leaf_source});

  auto l3_middle_tree_source = std::make_shared<TreeDataSource>(
      DataSourceVector{kChildPerNode, l2_leaf_tree_source});

  auto root_source = std::make_shared<TreeDataSource>(
      DataSourceVector{kChildPerNode, l3_middle_tree_source});

  AssertDataSourceEquals(reader.get(), root_source.get());
}

class TestDataset : public DatasetFixtureMixin {};

TEST_F(TestDataset, TrivialScan) {
  constexpr int64_t kNumberFragments = 4;
  constexpr int64_t kNumberBatches = 16;
  constexpr int64_t kBatchSize = 1024;

  SetSchema({field("i32", int32()), field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, schema_);

  std::vector<std::shared_ptr<RecordBatch>> batches{kNumberBatches, batch};
  auto fragment = std::make_shared<SimpleDataFragment>(batches, options_);
  DataFragmentVector fragments{kNumberFragments, fragment};

  DataSourceVector sources = {
      std::make_shared<SimpleDataSource>(fragments),
      std::make_shared<SimpleDataSource>(fragments),
  };

  const int64_t total_batches = sources.size() * kNumberFragments * kNumberBatches;
  auto reader = ConstantArrayGenerator::Repeat(total_batches, batch);

  ASSERT_OK_AND_ASSIGN(auto dataset, Dataset::Make(sources, schema_));
  AssertDatasetEquals(reader.get(), dataset.get());
}

TEST(TestProjector, MismatchedType) {
  constexpr int64_t kBatchSize = 1024;

  auto from_schema = schema({field("f64", float64())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, from_schema);

  auto to_schema = schema({field("f64", int32())});
  RecordBatchProjector projector(to_schema);

  auto result = projector.Project(*batch);
  ASSERT_RAISES(TypeError, result.status());
}

TEST(TestProjector, AugmentWithNull) {
  constexpr int64_t kBatchSize = 1024;

  auto from_schema = schema({field("f64", float64()), field("b", boolean())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, from_schema);
  auto to_schema = schema({field("i32", int32()), field("f64", float64())});

  RecordBatchProjector projector(to_schema);

  std::shared_ptr<Array> null_i32;
  ASSERT_OK(MakeArrayOfNull(int32(), batch->num_rows(), &null_i32));
  auto expected_batch =
      RecordBatch::Make(to_schema, batch->num_rows(), {null_i32, batch->column(0)});

  ASSERT_OK_AND_ASSIGN(auto reconciled_batch, projector.Project(*batch));
  AssertBatchesEqual(*expected_batch, *reconciled_batch);
}

TEST(TestProjector, AugmentWithScalar) {
  constexpr int64_t kBatchSize = 1024;
  constexpr int32_t kScalarValue = 3;

  auto from_schema = schema({field("f64", float64()), field("b", boolean())});
  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, from_schema);
  auto to_schema = schema({field("i32", int32()), field("f64", float64())});

  auto scalar_i32 = std::make_shared<Int32Scalar>(kScalarValue);

  RecordBatchProjector projector(to_schema);
  ASSERT_OK(projector.SetDefaultValue(to_schema->GetFieldIndex("i32"), scalar_i32));

  ASSERT_OK_AND_ASSIGN(auto array_i32,
                       ArrayFromBuilderVisitor(int32(), kBatchSize, [](Int32Builder* b) {
                         b->UnsafeAppend(kScalarValue);
                       }));

  auto expected_batch =
      RecordBatch::Make(to_schema, batch->num_rows(), {array_i32, batch->column(0)});

  ASSERT_OK_AND_ASSIGN(auto reconciled_batch, projector.Project(*batch));
  AssertBatchesEqual(*expected_batch, *reconciled_batch);
}

TEST(TestProjector, NonTrivial) {
  constexpr int64_t kBatchSize = 1024;

  constexpr float kScalarValue = 3.14f;

  auto from_schema =
      schema({field("i8", int8()), field("u8", uint8()), field("i16", int16()),
              field("u16", uint16()), field("i32", int32()), field("u32", uint32())});

  auto batch = ConstantArrayGenerator::Zeroes(kBatchSize, from_schema);

  auto to_schema =
      schema({field("i32", int32()), field("f64", float64()), field("u16", uint16()),
              field("u8", uint8()), field("b", boolean()), field("u32", uint32()),
              field("f32", float32())});

  auto scalar_f32 = std::make_shared<FloatScalar>(kScalarValue);
  auto scalar_f64 = std::make_shared<DoubleScalar>(kScalarValue);

  RecordBatchProjector projector(to_schema);
  ASSERT_OK(projector.SetDefaultValue(to_schema->GetFieldIndex("f64"), scalar_f64));
  ASSERT_OK(projector.SetDefaultValue(to_schema->GetFieldIndex("f32"), scalar_f32));

  ASSERT_OK_AND_ASSIGN(
      auto array_f32, ArrayFromBuilderVisitor(float32(), kBatchSize, [](FloatBuilder* b) {
        b->UnsafeAppend(kScalarValue);
      }));
  ASSERT_OK_AND_ASSIGN(auto array_f64, ArrayFromBuilderVisitor(
                                           float64(), kBatchSize, [](DoubleBuilder* b) {
                                             b->UnsafeAppend(kScalarValue);
                                           }));
  ASSERT_OK_AND_ASSIGN(
      auto null_b, ArrayFromBuilderVisitor(boolean(), kBatchSize, [](BooleanBuilder* b) {
        b->UnsafeAppendNull();
      }));

  auto expected_batch = RecordBatch::Make(
      to_schema, batch->num_rows(),
      {batch->GetColumnByName("i32"), array_f64, batch->GetColumnByName("u16"),
       batch->GetColumnByName("u8"), null_b, batch->GetColumnByName("u32"), array_f32});

  ASSERT_OK_AND_ASSIGN(auto reconciled_batch, projector.Project(*batch));
  AssertBatchesEqual(*expected_batch, *reconciled_batch);
}

class TestEndToEnd : public TestDataset {
  void SetUp() {
    bool nullable = false;
    SetSchema({
        field("region", utf8(), nullable),
        field("model", utf8(), nullable),
        field("sales", float64(), nullable),
        // partition columns
        field("year", int32()),
        field("month", int32()),
        field("country", utf8()),
    });

    using PathAndContent = std::vector<std::pair<std::string, std::string>>;
    auto files = PathAndContent{
        {"/dataset/2018/01/US/dat.json", R"([
        {"region": "NY", "model": "3", "sales": 742.0},
        {"region": "NY", "model": "S", "sales": 304.125},
        {"region": "NY", "model": "X", "sales": 136.25},
        {"region": "NY", "model": "Y", "sales": 27.5}
      ])"},
        {"/dataset/2018/01/CA/dat.json", R"([
        {"region": "CA", "model": "3", "sales": 512},
        {"region": "CA", "model": "S", "sales": 978},
        {"region": "CA", "model": "X", "sales": 1.0},
        {"region": "CA", "model": "Y", "sales": 69}
      ])"},
        {"/dataset/2019/01/US/dat.json", R"([
        {"region": "QC", "model": "3", "sales": 273.5},
        {"region": "QC", "model": "S", "sales": 13},
        {"region": "QC", "model": "X", "sales": 54},
        {"region": "QC", "model": "Y", "sales": 21}
      ])"},
        {"/dataset/2019/01/CA/dat.json", R"([
        {"region": "QC", "model": "3", "sales": 152.25},
        {"region": "QC", "model": "S", "sales": 10},
        {"region": "QC", "model": "X", "sales": 42},
        {"region": "QC", "model": "Y", "sales": 37}
      ])"},
        {"/dataset/.pesky", "garbage content"},
    };

    auto mock_fs = std::make_shared<fs::internal::MockFileSystem>(fs::kNoTime);
    for (const auto& f : files) {
      ARROW_EXPECT_OK(mock_fs->CreateFile(f.first, f.second, /* recursive */ true));
    }

    fs_ = mock_fs;
  }

 protected:
  std::shared_ptr<fs::FileSystem> fs_;
};

TEST_F(TestEndToEnd, EndToEndSingleSource) {
  // The dataset API is divided in 3 parts:
  //  - Creation
  //  - Querying
  //  - Consuming

  // Creation.
  //
  // A Dataset is the union of one or more DataSources with the same schema.
  // Example of DataSource, FileSystemDataSource, OdbcDataSource,
  // FlightDataSource.

  // A DataSource is composed of DataFragments. Each DataFragment can yield
  // multiple RecordBatches. DataSources can be created manually or "discovered"
  // via the DataSourceDiscovery interface.
  DataSourceDiscoveryPtr discovery;

  // The user must specify which FileFormat is used to create FileFragments.
  // This option is specific to FileSystemDataSource (and the builder).
  auto format_schema = SchemaFromColumnNames(schema_, {"region", "model", "sales"});
  auto format = std::make_shared<JSONRecordBatchFileFormat>(format_schema);

  // A selector is used to crawl files and directories of a
  // filesystem. If the options in Selector are not enough, the
  // FileSystemDataSourceDiscovery class also supports an explicit list of
  // fs::FileStats instead of the selector.
  fs::Selector s;
  s.base_dir = "/dataset";
  s.recursive = true;

  // Further options can be given to the discovery mechanism via the
  // FileSystemDiscoveryOptions configuration class. See the docstring for more
  // information.
  FileSystemDiscoveryOptions options;
  options.ignore_prefixes = {"."};

  ASSERT_OK_AND_ASSIGN(discovery,
                       FileSystemDataSourceDiscovery::Make(fs_, s, format, options));

  // Partitions expressions can be discovered for DataSource and DataFragments.
  // This metadata is then used in conjuction with the query filter to apply
  // the pushdown predicate optimization.
  auto partition_schema = SchemaFromColumnNames(schema_, {"year", "month", "country"});
  // The SchemaPartitionScheme is a simple scheme where the path is split with
  // the directory separator character and the components are typed and named
  // with the equivalent index in the schema, e.g.
  // (with the previous defined schema):
  //
  // - "/2019" -> {"year": 2019}
  // - "/2019/01 -> {"year": 2019, "month": 1}
  // - "/2019/01/CA -> {"year": 2019, "month": 1, "country": "CA"}
  // - "/2019/01/CA/a_file.json -> {"year": 2019, "month": 1, "country": "CA"}
  auto partition_scheme = std::make_shared<SchemaPartitionScheme>(partition_schema);
  ASSERT_OK(discovery->SetPartitionScheme(partition_scheme));

  // DataFragments might have compatible but slightly different schemas, e.g.
  // schema evolved by adding/renaming columns. In this case, the schema is
  // passed to the dataset constructor.
  ASSERT_OK_AND_ASSIGN(auto inspected_schema, discovery->Inspect());
  EXPECT_EQ(*schema_, *inspected_schema);

  // Build the DataSource where partitions are attached to fragments (files).
  ASSERT_OK_AND_ASSIGN(auto datasource, discovery->Finish());

  // Create the Dataset from our single DataSource.
  ASSERT_OK_AND_ASSIGN(auto dataset, Dataset::Make({datasource}, inspected_schema));

  // Querying.
  //
  // The Scan operator materializes data from io into memory. Avoiding data
  // transfer is a critical optimization done by analytical engine. Thus, a
  // Scan can take multiple options, notably a subset of columns and a filter
  // expression.
  ASSERT_OK_AND_ASSIGN(auto scanner_builder, dataset->NewScan());

  // An optional subset of columns can be provided. This will trickle to
  // DataFragment drivers. The net effect is that only columns of interest will
  // be materialized if the DataFragment supports it. This is the major benefit
  // of using a column-major format versus a row-major format.
  //
  // This API decouples the DataSource/DataFragment implementation and column
  // projection from the query part.
  //
  // For example, a ParquetFileDataFragment may read the necessary byte ranges
  // exclusively, ranges, or an OdbcDataFragment could convert the projection to a SELECT
  // statement. The CsvFileDataFragment wouldn't benefit from this as much, but
  // can still benefit from skipping conversion of unneeded columns.
  std::vector<std::string> columns{"sales", "model", "country"};
  ASSERT_OK(scanner_builder->Project(columns));

  // An optional filter expression may also be specified. The filter expression
  // is evaluated against input rows. Only rows for which the filter evauates to true are
  // yielded. Predicate pushdown optimizations are applied using partition information if
  // available.
  //
  // This API decouples predicate pushdown from the DataSource implementation
  // and partition discovery.
  //
  // The following filter tests both predicate pushdown and post filtering
  // without partition information because `year` is a partition and `sales` is
  // not.
  auto filter = ("year"_ == 2019 && "sales"_ > 100.0);
  ASSERT_OK(scanner_builder->Filter(filter));

  ASSERT_OK_AND_ASSIGN(auto scanner, scanner_builder->Finish());
  // In the simplest case, consumption is simply conversion to a Table.
  ASSERT_OK_AND_ASSIGN(auto table, scanner->ToTable());

  using row_type = std::tuple<double, std::string, util::optional<std::string>>;
  std::vector<row_type> rows{
      row_type{152.25, "3", "CA"},
      row_type{273.5, "3", "US"},
  };
  std::shared_ptr<Table> expected;
  ASSERT_OK(stl::TableFromTupleRange(default_memory_pool(), rows, columns, &expected));
  AssertTablesEqual(*expected, *table, false, true);
}

class TestSchemaUnification : public TestDataset {
 public:
  using i32 = util::optional<int32_t>;

  void SetUp() {
    using PathAndContent = std::vector<std::pair<std::string, std::string>>;

    // The following test creates 2 data source with divergent but compatible
    // schemas. Each data source have a common partition scheme where the
    // fields are not materialized in the data fragments.
    //
    // Each data source is composed of 2 data fragments with divergent but
    // compatible schemas. The data fragment within a data source share at
    // least one column.
    //
    // Thus, the fixture helps verifying various scenarios where the Scanner
    // must fix the RecordBatches to align with the final unified schema exposed
    // to the consumer.
    constexpr auto ds1_df1 = "/dataset/alpha/part_ds=1/part_df=1/data.json";
    constexpr auto ds1_df2 = "/dataset/alpha/part_ds=1/part_df=2/data.json";
    constexpr auto ds2_df1 = "/dataset/beta/part_ds=2/part_df=1/data.json";
    constexpr auto ds2_df2 = "/dataset/beta/part_ds=2/part_df=2/data.json";
    auto files = PathAndContent{
        // First DataSource
        {ds1_df1, R"([{"phy_1": 111, "phy_2": 211}])"},
        {ds1_df2, R"([{"phy_2": 212, "phy_3": 312}])"},
        // Second DataSource
        {ds2_df1, R"([{"phy_3": 321, "phy_4": 421}])"},
        {ds2_df2, R"([{"phy_4": 422, "phy_2": 222}])"},
    };

    auto mock_fs = std::make_shared<fs::internal::MockFileSystem>(fs::kNoTime);
    for (const auto& f : files) {
      ARROW_EXPECT_OK(mock_fs->CreateFile(f.first, f.second, /* recursive */ true));
    }
    fs_ = mock_fs;

    auto get_source = [this](std::string base) -> Result<std::shared_ptr<DataSource>> {
      fs::Selector s;
      s.base_dir = base;
      s.recursive = true;

      auto resolver = [this](const FileSource& source) -> std::shared_ptr<Schema> {
        auto path = source.path();
        // A different schema for each data fragment.
        if (path == ds1_df1) {
          return SchemaFromNames({"phy_1", "phy_2"});
        } else if (path == ds1_df2) {
          return SchemaFromNames({"phy_2", "phy_3"});
        } else if (path == ds2_df1) {
          return SchemaFromNames({"phy_3", "phy_4"});
        } else if (path == ds2_df2) {
          return SchemaFromNames({"phy_4", "phy_2"});
        }

        return nullptr;
      };

      auto format = std::make_shared<JSONRecordBatchFileFormat>(resolver);
      ARROW_ASSIGN_OR_RAISE(auto discovery,
                            FileSystemDataSourceDiscovery::Make(fs_, s, format, {}));

      auto scheme_schema = SchemaFromNames({"part_ds", "part_df"});
      auto partition_scheme = std::make_shared<HivePartitionScheme>(scheme_schema);
      RETURN_NOT_OK(discovery->SetPartitionScheme(partition_scheme));

      return discovery->Finish();
    };

    schema_ = SchemaFromNames({"phy_1", "phy_2", "phy_3", "phy_4", "part_ds", "part_df"});
    ASSERT_OK_AND_ASSIGN(auto ds1, get_source("/dataset/alpha"));
    ASSERT_OK_AND_ASSIGN(auto ds2, get_source("/dataset/beta"));
    ASSERT_OK_AND_ASSIGN(dataset_, Dataset::Make({ds1, ds2}, schema_));
  }

  std::shared_ptr<Schema> SchemaFromNames(const std::vector<std::string> names) {
    std::vector<std::shared_ptr<Field>> fields;
    for (const auto& name : names) {
      fields.push_back(field(name, int32()));
    }

    return schema(fields);
  }

  template <typename TupleType>
  void AssertScanEquals(std::shared_ptr<Scanner> scanner,
                        const std::vector<TupleType>& expected_rows) {
    std::vector<std::string> columns;
    for (const auto& field : scanner->schema()->fields()) {
      columns.push_back(field->name());
    }

    ASSERT_OK_AND_ASSIGN(auto actual, scanner->ToTable());
    std::shared_ptr<Table> expected;
    ASSERT_OK(stl::TableFromTupleRange(default_memory_pool(), expected_rows, columns,
                                       &expected));
    AssertTablesEqual(*expected, *actual, false, true);
  }

  template <typename TupleType>
  void AssertBuilderEquals(std::shared_ptr<ScannerBuilder> builder,
                           const std::vector<TupleType>& expected_rows) {
    ASSERT_OK_AND_ASSIGN(auto scanner, builder->Finish());
    AssertScanEquals(scanner, expected_rows);
  }

 protected:
  std::shared_ptr<fs::FileSystem> fs_;
  std::shared_ptr<Dataset> dataset_;
};

using nonstd::nullopt;

TEST_F(TestSchemaUnification, SelectStar) {
  // This is a `SELECT * FROM dataset` where it ensures:
  //
  // - proper re-ordering of columns
  // - materializing missing physical columns in DataFragments
  // - materializing missing partition columns extracted from PartitionScheme
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());

  using TupleType = std::tuple<i32, i32, i32, i32, i32, i32>;
  std::vector<TupleType> rows = {
      {111, 211, nullopt, nullopt, 1, 1},
      {nullopt, 212, 312, nullopt, 1, 2},
      {nullopt, nullopt, 321, 421, 2, 1},
      {nullopt, 222, nullopt, 422, 2, 2},
  };

  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectPhysicalColumns) {
  // Same as above, but scoped to physical columns.
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Project({"phy_1", "phy_2", "phy_3", "phy_4"}));

  using TupleType = std::tuple<i32, i32, i32, i32>;
  std::vector<TupleType> rows = {
      {111, 211, nullopt, nullopt},
      {nullopt, 212, 312, nullopt},
      {nullopt, nullopt, 321, 421},
      {nullopt, 222, nullopt, 422},
  };

  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectSomeReorderedPhysicalColumns) {
  // Select physical columns in a different order than physical DataFragments
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Project({"phy_2", "phy_1", "phy_4"}));

  using TupleType = std::tuple<i32, i32, i32>;
  std::vector<TupleType> rows = {
      {211, 111, nullopt},
      {212, nullopt, nullopt},
      {nullopt, nullopt, 421},
      {222, nullopt, 422},
  };

  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectPhysicalColumnsFilterPartitionColumn) {
  // Select a subset of physical column with a filter on a missing physical
  // column and a partition column, it ensures:
  //
  // - Can filter on virtual and physical columns with a non-trivial filter
  //   when some of the columns may not be materialized
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Project({"phy_2", "phy_3", "phy_4"}));
  ASSERT_OK(scan_builder->Filter(("part_df"_ == 1 && "phy_2"_ == 211) ||
                                 ("part_ds"_ == 2 && "phy_4"_ != 422)));

  using TupleType = std::tuple<i32, i32, i32>;
  std::vector<TupleType> rows = {
      {211, nullopt, nullopt},
      {nullopt, 321, 421},
  };

  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectPartitionColumns) {
  // Selects partition (virtual) columns, it ensures:
  //
  // - virtual column are materialized
  // - DataFragment yield the right number of rows even if no column is selected
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Project({"part_ds", "part_df"}));
  using TupleType = std::tuple<i32, i32>;
  std::vector<TupleType> rows = {
      {1, 1},
      {1, 2},
      {2, 1},
      {2, 2},
  };
  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectPartitionColumnsFilterPhysicalColumn) {
  // Selects re-ordered virtual columns with a filter on a physical columns
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Filter("phy_1"_ == 111));

  ASSERT_OK(scan_builder->Project({"part_df", "part_ds"}));
  using TupleType = std::tuple<i32, i32>;
  std::vector<TupleType> rows = {
      {1, 1},
  };
  AssertBuilderEquals(scan_builder, rows);
}

TEST_F(TestSchemaUnification, SelectMixedColumnsAndFilter) {
  // Selects mix of phyical/virtual with a different order and uses a filter on
  // a physical column not selected.
  ASSERT_OK_AND_ASSIGN(auto scan_builder, dataset_->NewScan());
  ASSERT_OK(scan_builder->Filter("phy_2"_ >= 212));
  ASSERT_OK(scan_builder->Project({"part_df", "phy_3", "part_ds", "phy_1"}));

  using TupleType = std::tuple<i32, i32, i32, i32>;
  std::vector<TupleType> rows = {
      {2, 312, 1, nullopt},
      {2, nullopt, 2, nullopt},
  };
  AssertBuilderEquals(scan_builder, rows);
}

}  // namespace dataset
}  // namespace arrow
