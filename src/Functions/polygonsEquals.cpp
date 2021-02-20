#include <Functions/FunctionFactory.h>
#include <Functions/geometryConverters.h>

#include <boost/geometry.hpp>
#include <boost/geometry/geometries/point_xy.hpp>
#include <boost/geometry/geometries/polygon.hpp>

#include <common/logger_useful.h>

#include <Columns/ColumnArray.h>
#include <Columns/ColumnTuple.h>
#include <Columns/ColumnConst.h>
#include <Columns/ColumnsNumber.h>
#include <DataTypes/DataTypesNumber.h>
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypeCustomGeo.h>

#include <memory>
#include <utility>

namespace DB
{

template <typename Point>
class FunctionPolygonsEquals : public IFunction
{
public:
    static const char * name;

    explicit FunctionPolygonsEquals() = default;

    static FunctionPtr create(const Context &)
    {
        return std::make_shared<FunctionPolygonsEquals>();
    }

    String getName() const override
    {
        return name;
    }

    bool isVariadic() const override
    {
        return false;
    }

    size_t getNumberOfArguments() const override
    {
        return 2;
    }

    DataTypePtr getReturnTypeImpl(const DataTypes &) const override
    {
        return std::make_shared<DataTypeUInt8>();
    }

    ColumnPtr executeImpl(const ColumnsWithTypeAndName & arguments, const DataTypePtr & /*result_type*/, size_t input_rows_count) const override
    {
        auto res_column = ColumnUInt8::create();
        auto & res_data = res_column->getData();
        res_data.reserve(input_rows_count);

        callOnTwoGeometryDataTypes<Point>(arguments[0].type, arguments[1].type, [&](const auto & left_type, const auto & right_type)
        {
            using LeftConverterType = std::decay_t<decltype(left_type)>;
            using RightConverterType = std::decay_t<decltype(right_type)>;

            using LeftConverter = typename LeftConverterType::Type;
            using RightConverter = typename RightConverterType::Type;

            auto first = LeftConverter(arguments[0].column->convertToFullColumnIfConst()).convert();
            auto second = RightConverter(arguments[1].column->convertToFullColumnIfConst()).convert();

            for (size_t i = 0; i < input_rows_count; i++)
            {
                boost::geometry::correct(first[i]);
                boost::geometry::correct(second[i]);

                /// Main work here.
                res_data.emplace_back(boost::geometry::equals(first[i], second[i]));
            }
        }
        );

        return res_column;
    }

    bool useDefaultImplementationForConstants() const override
    {
        return true;
    }
};


template <>
const char * FunctionPolygonsEquals<CartesianPoint>::name = "polygonsEqualsCartesian";


void registerFunctionPolygonsEquals(FunctionFactory & factory)
{
    factory.registerFunction<FunctionPolygonsEquals<CartesianPoint>>();
}

}
