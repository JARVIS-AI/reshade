/**
 * Copyright (C) 2014 Patrick Mours. All rights reserved.
 * License: https://github.com/crosire/reshade#license
 */

#pragma once

#include <memory>
#include "effect_lexer.hpp"
#include "variant.hpp"
#include "source_location.hpp"
#include "runtime_objects.hpp"
#include <spirv.hpp>
#include <algorithm>
#include <assert.h>

namespace reshadefx
{
	struct spv_node
	{
		spv::Op op = spv::OpNop;
		spv::Id result = 0;
		spv::Id result_type = 0;
		std::vector<spv::Id> operands;
		size_t index = -1;
		location location = {};

		explicit spv_node(spv::Op op = spv::OpNop) : op(op) { }
		explicit spv_node(spv::Op op, spv::Id result) : op(op), result_type(result) { }
		explicit spv_node(spv::Op op, spv::Id type, spv::Id result) : op(op), result_type(type), result(result) { }

		spv_node &add(spv::Id operand)
		{
			operands.push_back(operand);
			return *this;
		}
		spv_node &add_string(const char *str)
		{
			uint32_t word = 0;

			while (*str || word & 0x10000000)
			{
				word = 0;

				for (uint32_t i = 0; i < 4 && *str; ++i)
				{
					reinterpret_cast<uint8_t *>(&word)[i] = *str++;
				}

				add(word);
			}

			return *this;
		}
	};

	enum qualifier : unsigned int
	{
		// Storage
		qualifier_extern = 1 << 0,
		qualifier_static = 1 << 1,
		qualifier_uniform = 1 << 2,
		qualifier_volatile = 1 << 3,
		qualifier_precise = 1 << 4,
		qualifier_in = 1 << 5,
		qualifier_out = 1 << 6,
		qualifier_inout = qualifier_in | qualifier_out,

		// Modifier
		qualifier_const = 1 << 8,

		// Interpolation
		qualifier_linear = 1 << 10,
		qualifier_noperspective = 1 << 11,
		qualifier_centroid = 1 << 12,
		qualifier_nointerpolation = 1 << 13,
	};

	struct type_info
	{
		static unsigned int rank(const type_info &src, const type_info &dst);

		bool has(qualifier qualifier) const { return (qualifiers & qualifier) == qualifier; }
		bool is_array() const { return array_length != 0; }
		bool is_scalar() const { return !is_array() && !is_matrix() && !is_vector() && is_numeric(); }
		bool is_vector() const { return rows > 1 && cols == 1; }
		bool is_matrix() const { return rows >= 1 && cols > 1; }
		bool is_numeric() const { return is_boolean() || is_integral() || is_floating_point(); }
		bool is_void() const { return base == spv::OpTypeVoid; }
		bool is_boolean() const { return base == spv::OpTypeBool; }
		bool is_integral() const { return base == spv::OpTypeInt; }
		bool is_floating_point() const { return base == spv::OpTypeFloat; }
		bool is_struct() const { return base == spv::OpTypeStruct; }
		bool is_image() const { return base == spv::OpTypeImage; }
		bool is_sampled_image() const { return base == spv::OpTypeSampledImage; }

		spv::Op base;
		unsigned int size : 8;
		unsigned int rows : 4;
		unsigned int cols : 4;
		bool is_signed = false;
		bool is_pointer = false;
		unsigned int qualifiers = 0;
		int array_length = 0;
		spv::Id definition = 0;
		spv::Id array_lenghth_expression = 0;
	};

	inline bool operator==(const type_info &lhs, const type_info &rhs)
	{
		return lhs.base == rhs.base && lhs.size == rhs.size && lhs.rows == rhs.rows && lhs.cols == rhs.cols && lhs.is_signed == rhs.is_signed && lhs.array_length == rhs.array_length && lhs.definition == rhs.definition && lhs.is_pointer == rhs.is_pointer;
	}

	struct struct_info
	{
		std::vector<std::pair<std::string, type_info>> field_list;
	};
	struct function_info
	{
		type_info return_type;
		std::string name;
		std::string unique_name;
		std::vector<type_info> parameter_list;
		std::string return_semantic;
		spv::Id definition;
	};
	struct variable_info
	{
		std::unordered_map<std::string, reshade::variant> annotation_list;
		spv::Id texture;
		unsigned int width = 1, height = 1, depth = 1, levels = 1;
		bool srgb_texture;
		reshade::texture_format format = reshade::texture_format::rgba8;
		reshade::texture_filter filter = reshade::texture_filter::min_mag_mip_linear;
		reshade::texture_address_mode address_u = reshade::texture_address_mode::clamp;
		reshade::texture_address_mode address_v = reshade::texture_address_mode::clamp;
		reshade::texture_address_mode address_w = reshade::texture_address_mode::clamp;
		float min_lod, max_lod = FLT_MAX, lod_bias;
	};

	struct pass_properties
	{
		enum : unsigned int
		{
			NONE = 0,

			ZERO = 0,
			ONE = 1,
			SRCCOLOR,
			INVSRCCOLOR,
			SRCALPHA,
			INVSRCALPHA,
			DESTALPHA,
			INVDESTALPHA,
			DESTCOLOR,
			INVDESTCOLOR,

			ADD = 1,
			SUBTRACT,
			REVSUBTRACT,
			MIN,
			MAX,

			KEEP = 1,
			REPLACE = 3,
			INCRSAT,
			DECRSAT,
			INVERT,
			INCR,
			DECR,

			NEVER = 1,
			LESS,
			EQUAL,
			LESSEQUAL,
			GREATER,
			NOTEQUAL,
			GREATEREQUAL,
			ALWAYS
		};

		location location;
		std::string name;
		std::unordered_map<std::string, reshade::variant> annotation_list;
		spv::Id render_targets[8] = {};
		spv::Id vertex_shader = 0, pixel_shader = 0;
		bool clear_render_targets = true, srgb_write_enable, blend_enable, stencil_enable;
		unsigned char color_write_mask = 0xF, stencil_read_mask = 0xFF, stencil_write_mask = 0xFF;
		unsigned int blend_op = ADD, blend_op_alpha = ADD, src_blend = ONE, dest_blend = ZERO, src_blend_alpha = ONE, dest_blend_alpha = ZERO;
		unsigned int stencil_comparison_func = ALWAYS, stencil_reference_value, stencil_op_pass = KEEP, stencil_op_fail = KEEP, stencil_op_depth_fail = KEEP;
	};
	struct technique_properties
	{
		location location;
		std::string name, unique_name;
		std::unordered_map<std::string, reshade::variant> annotation_list;
		std::vector<pass_properties> pass_list;
	};

	struct spv_section
	{
		std::vector<spv_node> instructions;
	};

	/// <summary>
	/// A parser for the ReShade FX language.
	/// </summary>
	class parser
	{
	public:
		/// <summary>
		/// Construct a new parser instance.
		/// </summary>
		parser();
		parser(const parser &) = delete;
		~parser();

		parser &operator=(const parser &) = delete;

		/// <summary>
		/// Gets the list of error messages.
		/// </summary>
		const std::string &errors() const { return _errors; }

		/// <summary>
		/// Parse the provided input string.
		/// </summary>
		/// <param name="source">The string to analyze.</param>
		/// <returns>A boolean value indicating whether parsing was successful or not.</returns>
		bool run(const std::string &source);

	private:
		void error(const location &location, unsigned int code, const std::string &message);
		void warning(const location &location, unsigned int code, const std::string &message);

		void backup();
		void restore();

		bool peek(tokenid tokid) const;
		bool peek(char tok) const { return peek(static_cast<tokenid>(tok)); }
		bool peek_multary_op(spv::Op &op, unsigned int &precedence) const;
		void consume();
		void consume_until(tokenid tokid);
		void consume_until(char tok) { return consume_until(static_cast<tokenid>(tok)); }
		bool accept(tokenid tokid);
		bool accept(char tok) { return accept(static_cast<tokenid>(tok)); }
		bool expect(tokenid tokid);
		bool expect(char tok) { return expect(static_cast<tokenid>(tok)); }

		bool accept_type_class(type_info &type);
		bool accept_type_qualifiers(type_info &type);

		bool accept_unary_op(spv::Op &op);
		bool accept_postfix_op(spv::Op &op);
		bool accept_assignment_op(spv::Op &op);

		bool parse_top_level();
		bool parse_namespace();
		bool parse_type(type_info &type);

		bool parse_expression(spv_section &section, spv::Id &node, type_info &type);
		bool parse_expression_unary(spv_section &section, spv::Id &node, type_info &type);
		bool parse_expression_multary(spv_section &section, spv::Id &node, type_info &type, unsigned int precedence = 0);
		bool parse_expression_assignment(spv_section &section, spv::Id &node, type_info &type);

		bool parse_statement(spv_section &section, bool scoped = true);
		bool parse_statement_block(spv_section &section, spv::Id &label, bool scoped = true);

		bool parse_array(int &size);
		bool parse_annotations(std::unordered_map<std::string, reshade::variant> &annotations);

		bool parse_struct(spv::Id &structure);
		bool parse_function_declaration(type_info &type, std::string name, spv::Id &function);
		bool parse_variable_declaration(spv_section &section, type_info &type, std::string name, spv::Id &variable, bool global = false);
		bool parse_variable_assignment(spv_section &section, spv::Id &node, type_info &type);
		bool parse_variable_properties(variable_info &props);
		bool parse_variable_properties_expression(spv::Id &expression, type_info &type);
		bool parse_technique(technique_properties &technique);
		bool parse_technique_pass(pass_properties &pass);
		bool parse_technique_pass_expression(spv::Id &expression, type_info &type);

		spv_section _entries;
		spv_section _strings;
		spv_section _annotations;
		spv_section _variables;
		spv_section _function_section;
		spv_section _temporary;

		std::unordered_map<spv::Id, struct_info> _structs;
		std::vector<std::unique_ptr<function_info>> _functions;
		std::vector<technique_properties> techniques;

		std::vector<std::pair<spv_section *, size_t>> _id_lookup;
		std::vector<std::pair<type_info, spv::Id>> _type_lookup;
		std::unordered_map<spv::Op, std::vector<type_info>> _type_lookup2;


		spv::Id _next_id = 100;

		spv::Id add_node(spv_section &section, location loc, spv::Op op, spv::Id type = 0)
		{
			spv_node &instruction = add_node_without_result(section, loc, op);
			instruction.result = _next_id++;
			instruction.result_type = type;

			_id_lookup.push_back({ &section, instruction.index });

			return instruction.result;
		}
		spv_node &add_node_without_result(spv_section &section, location loc, spv::Op op)
		{
			spv_node &instruction = section.instructions.emplace_back();
			instruction.op = op;
			instruction.index = section.instructions.size() - 1;
			instruction.location = loc;

			return instruction;
		}

		spv::Id add_cast_node(spv_section &section, location loc, const type_info &from, const type_info &to, spv::Id input)
		{
			spv::Id result = input;

			switch (to.base)
			{
			case spv::OpTypeInt:
				assert(from.is_floating_point());
				result = add_node(section, loc, to.is_signed ? spv::OpConvertFToS : spv::OpConvertFToU, convert_type(to));
				lookup_id(result).add(input);
				break;
			case spv::OpTypeFloat:
				assert(from.is_integral());
				result = add_node(section, loc, from.is_signed ? spv::OpConvertSToF : spv::OpConvertUToF, convert_type(to));
				lookup_id(result).add(input);
				break;
			}

			return result;
		}

		spv::Id convert_type(const type_info &info)
		{
			if (auto it = std::find_if(_type_lookup.begin(), _type_lookup.end(), [&info](auto &x) { return x.first == info; }); it != _type_lookup.end())
				return it->second;

			spv::Id type;

			if (info.is_array())
			{
				spv::Id elemtype = convert_type(type_info { info.base, info.size, info.rows, info.cols, info.is_signed });

				// TODO: Array stride
				if (info.array_length > 0) // Sized array
				{
					//assert(info.array_lenghth_expression);

					type = add_node(_variables, {}, spv::OpTypeArray);
					lookup_id(type)
						.add(elemtype)
						.add(info.array_lenghth_expression);
				}
				else // Dynamic array
				{
					type = add_node(_variables, {}, spv::OpTypeRuntimeArray);
					lookup_id(type)
						.add(elemtype);
				}
			}
			else if (info.is_pointer)
			{
				spv::Id elemtype = convert_type(type_info { info.base, info.size, info.rows, info.cols, info.is_signed });

				type = add_node(_variables, {}, spv::OpTypePointer);
				lookup_id(type)
					.add(spv::StorageClassFunction)
					.add(elemtype);
			}
			else
			{
				if (info.is_vector())
				{
					const spv::Id elemtype = convert_type(type_info { info.base, info.size, 1, 1, info.is_signed });

					type = add_node(_variables, {}, spv::OpTypeVector);
					lookup_id(type)
						.add(elemtype)
						.add(info.rows);
				}
				else if (info.is_matrix())
				{
					const spv::Id elemtype = convert_type(type_info { info.base, info.size, info.rows, 1, info.is_signed });

					type = add_node(_variables, {}, spv::OpTypeMatrix);
					lookup_id(type)
						.add(elemtype)
						.add(info.cols);
				}
				else
				{
					switch (info.base)
					{
					case spv::OpTypeVoid:
						type = add_node(_variables, {}, spv::OpTypeVoid);
						break;
					case spv::OpTypeBool:
						type = add_node(_variables, {}, spv::OpTypeBool);
						break;
					case spv::OpTypeFloat:
						type = add_node(_variables, {}, spv::OpTypeFloat);
						lookup_id(type)
							.add(info.size);
						break;
					case spv::OpTypeInt:
						type = add_node(_variables, {}, spv::OpTypeInt);
						lookup_id(type)
							.add(info.size)
							.add(info.is_signed ? 1 : 0);
						break;
					case spv::OpTypeStruct:
						type = info.definition;
						break;
					case spv::OpTypeImage:
						type = add_node(_variables, {}, spv::OpTypeImage);
						lookup_id(type)
							.add(info.definition) // Sampled Type
							.add(spv::Dim2D)
							.add(0) // Not a depth image
							.add(0) // Not an array
							.add(0) // Not multi-sampled
							.add(1) // Will be used with a sampler
							.add(spv::ImageFormatRgba8);
						break;
					case spv::OpTypeSampledImage:
						type = add_node(_variables, {}, spv::OpTypeSampledImage);
						lookup_id(type)
							.add(info.definition);
						break;
					default:
						return 0;
					}
				}
			}

			_type_lookup.push_back({ info, type });;

			return type;
		}
		spv::Id convert_type(const function_info &info)
		{
			spv::Id return_type = convert_type(info.return_type);
			std::vector<spv::Id> param_types;
			for (const auto &param : info.parameter_list)
				param_types.push_back(convert_type(param));

			spv::Id type = add_node(_variables, {}, spv::OpTypeFunction);
			lookup_id(type).add(return_type);
			for (auto param_type : param_types)
				lookup_id(type).add(param_type);
			return type;
		}

		spv::Id convert_constant(const type_info &type, uint32_t value)
		{
			if (value == 0)
			{
				return add_node(_variables, {}, spv::OpConstantNull, convert_type(type));
			}
			else
			{
				auto id = add_node(_variables, {}, spv::OpConstant, convert_type(type));
				lookup_id(id)
					.add(value);
				return id;
			}
		}

		spv_node &lookup_id(spv::Id id)
		{
			return _id_lookup[id - 100].first->instructions[_id_lookup[id - 100].second];
		}

		std::string _errors;
		std::unique_ptr<lexer> _lexer, _lexer_backup;
		token _token, _token_next, _token_backup;
		std::unique_ptr<class symbol_table> _symbol_table;
	};
}
