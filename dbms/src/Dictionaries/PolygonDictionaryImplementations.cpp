#include "PolygonDictionaryImplementations.h"
#include "DictionaryFactory.h"
#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypesNumber.h>

#include <numeric>

namespace DB
{

SimplePolygonDictionary::SimplePolygonDictionary(
        const std::string & database_,
        const std::string & name_,
        const DictionaryStructure & dict_struct_,
        DictionarySourcePtr source_ptr_,
        const DictionaryLifetime dict_lifetime_,
        InputType input_type_,
        PointType point_type_):
        IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_)
{
}

std::shared_ptr<const IExternalLoadable> SimplePolygonDictionary::clone() const
{
    return std::make_shared<SimplePolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool SimplePolygonDictionary::find(const Point &point, size_t & id) const
{
    bool found = false;
    double area = 0;
    for (size_t i = 0; i < polygons.size(); ++i)
    {
        if (bg::covered_by(point, polygons[i]))
        {
            double new_area = areas[i];
            if (!found || new_area < area)
            {
                found = true;
                id = i;
                area = new_area;
            }
        }
    }
    return found;
}

GridPolygonDictionary::GridPolygonDictionary(
        const std::string & database_,
        const std::string & name_,
        const DictionaryStructure & dict_struct_,
        DictionarySourcePtr source_ptr_,
        const DictionaryLifetime dict_lifetime_,
        InputType input_type_,
        PointType point_type_):
        IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_),
        grid(kMinIntersections, kMaxDepth,polygons)
{
    std::vector<size_t> order(polygons.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](auto lhs, auto rhs) {
        return areas[lhs] < areas[rhs];
    });
    grid.init(order);
}

std::shared_ptr<const IExternalLoadable> GridPolygonDictionary::clone() const
{
    return std::make_shared<GridPolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool GridPolygonDictionary::find(const Point &point, size_t & id) const
{
    bool found = false;
    auto cell = grid.find(point.get<0>(), point.get<1>());
    if (cell)
    {
        for (auto candidate : cell->polygon_ids)
        {
            if (bg::covered_by(point, polygons[candidate]))
            {
                found = true;
                id = candidate;
                break;
            }
        }
    }
    return found;
}

template <class PolygonDictionary>
DictionaryPtr createLayout(const std::string &,
                           const DictionaryStructure & dict_struct,
                           const Poco::Util::AbstractConfiguration & config,
                           const std::string & config_prefix,
                           DictionarySourcePtr source_ptr)
{
    const String database = config.getString(config_prefix + ".database", "");
    const String name = config.getString(config_prefix + ".name");

    if (!dict_struct.key)
        throw Exception{"'key' is required for a polygon dictionary", ErrorCodes::BAD_ARGUMENTS};
    if (dict_struct.key->size() != 1)
        throw Exception{"The 'key' should consist of a single attribute for a polygon dictionary",
                        ErrorCodes::BAD_ARGUMENTS};
    IPolygonDictionary::InputType input_type;
    IPolygonDictionary::PointType point_type;
    const auto key_type = (*dict_struct.key)[0].type;
    const auto f64 = std::make_shared<DataTypeFloat64>();
    const auto multi_polygon_array = DataTypeArray(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(f64))));
    const auto multi_polygon_tuple = DataTypeArray(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(std::make_shared<DataTypeTuple>(std::vector<DataTypePtr>{f64, f64}))));
    const auto simple_polygon_array = DataTypeArray(std::make_shared<DataTypeArray>(f64));
    const auto simple_polygon_tuple = DataTypeArray(std::make_shared<DataTypeTuple>(std::vector<DataTypePtr>{f64, f64}));
    if (key_type->equals(multi_polygon_array))
    {
        input_type = IPolygonDictionary::InputType::MultiPolygon;
        point_type = IPolygonDictionary::PointType::Array;
    }
    else if (key_type->equals(multi_polygon_tuple))
    {
        input_type = IPolygonDictionary::InputType::MultiPolygon;
        point_type = IPolygonDictionary::PointType::Tuple;
    }
    else if (key_type->equals(simple_polygon_array))
    {
        input_type = IPolygonDictionary::InputType::SimplePolygon;
        point_type = IPolygonDictionary::PointType::Array;
    }
    else if (key_type->equals(simple_polygon_tuple))
    {
        input_type = IPolygonDictionary::InputType::SimplePolygon;
        point_type = IPolygonDictionary::PointType::Tuple;
    }
    else
        throw Exception{"The key type " + key_type->getName() +
                        " is not one of the following allowed types for a polygon dictionary: " +
                        multi_polygon_array.getName() + " " +
                        multi_polygon_tuple.getName() + " " +
                        simple_polygon_array.getName() + " " +
                        simple_polygon_tuple.getName() + " ",
                        ErrorCodes::BAD_ARGUMENTS};

    if (dict_struct.range_min || dict_struct.range_max)
        throw Exception{name
                        + ": elements range_min and range_max should be defined only "
                          "for a dictionary of layout 'range_hashed'",
                        ErrorCodes::BAD_ARGUMENTS};

    const DictionaryLifetime dict_lifetime{config, config_prefix + ".lifetime"};
    return std::make_unique<PolygonDictionary>(database, name, dict_struct, std::move(source_ptr), dict_lifetime, input_type, point_type);
};

void registerDictionaryPolygon(DictionaryFactory & factory)
{

    factory.registerLayout("polygon", createLayout<SimplePolygonDictionary>, true);
    factory.registerLayout("grid_polygon", createLayout<GridPolygonDictionary>, true);
}

}