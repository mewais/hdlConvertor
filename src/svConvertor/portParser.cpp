#include <hdlConvertor/svConvertor/portParser.h>

#include <tuple>
#include <hdlConvertor/conversion_exception.h>
#include <hdlConvertor/notImplementedLogger.h>
#include <hdlConvertor/hdlObjects/hdlDirection.h>
#include <hdlConvertor/hdlObjects/hdlStmAssign.h>
#include <hdlConvertor/svConvertor/exprParser.h>
#include <hdlConvertor/svConvertor/typeParser.h>
#include <hdlConvertor/svConvertor/attributeParser.h>
#include <hdlConvertor/svConvertor/utils.h>
#include <hdlConvertor/svConvertor/moduleParser.h>

namespace hdlConvertor {
namespace sv {

using namespace std;
using sv2017Parser = sv2017_antlr::sv2017Parser;
using namespace hdlObjects;

VerPortParser::VerPortParser(SVCommentParser &commentParser,
		vector<Non_ANSI_port_info_t> &_non_ansi_port_groups) :
		commentParser(commentParser), non_ansi_port_groups(
				_non_ansi_port_groups) {
}

unique_ptr<vector<unique_ptr<HdlVariableDef>>> VerPortParser::visitNonansi_port(
		sv2017Parser::Nonansi_portContext *ctx) {
	// nonansi_port:
	//     nonansi_port__expr
	//     | DOT identifier LPAREN ( nonansi_port__expr )? RPAREN;
	auto pi = ctx->identifier();
	auto _pe = ctx->nonansi_port__expr();
	unique_ptr<vector<unique_ptr<HdlVariableDef>>> pe = nullptr;
	if (_pe) {
		pe = visitNonansi_port__expr(_pe);
	} else {
		pe = make_unique<vector<unique_ptr<HdlVariableDef>>>();
		NotImplementedLogger::print(
				"Source_textParser.visitPort - empty port record", ctx);
	}
	if (pi) {
		// [todo] non_ansi_port_groups can not contains unique ptrs as the object are stored elsewhere
		// non_ansi_port_groups.push_back(
		// 		{ ctx->identifier()->getText(), clone(pe) });
	}
	return pe;
}
unique_ptr<vector<unique_ptr<HdlVariableDef>>> VerPortParser::visitNonansi_port__expr(
		sv2017Parser::Nonansi_port__exprContext *ctx) {
	// nonansi_port__expr:
	//     identifier_doted_index_at_end
	//     | LBRACE identifier_doted_index_at_end ( COMMA identifier_doted_index_at_end )* RBRACE
	// ;
	auto ports = make_unique<vector<unique_ptr<HdlVariableDef>>>();
	if (ctx->LBRACE()) {
		NotImplementedLogger::print(
				"VerPortParser.visitNonansi_port__expr - {}", ctx);
	}
	if (ctx) {
		VerExprParser ep(commentParser);
		for (auto pr : ctx->identifier_doted_index_at_end()) {
			auto id = ep.visitIdentifier_doted_index_at_end(pr);
			try {
				string id_name = id->extractStr();
				ports->push_back(
						make_unique<HdlVariableDef>(id_name, nullptr, nullptr));
			} catch (const std::runtime_error &err) {
				NotImplementedLogger::print(
						"VerPortParser.visitNonansi_port__expr variable which is not just identifier",
						pr);
			}
		}
	}
	return ports;
}
unique_ptr<vector<unique_ptr<HdlVariableDef>>> VerPortParser::visitList_of_port_declarations(
		sv2017Parser::List_of_port_declarationsContext *ctx) {
	// list_of_port_declarations:
	//     LPAREN
	//     (
	//       ( nonansi_port ( COMMA ( nonansi_port )? )* )
	//       | ( COMMA ( nonansi_port )? )+
	//       | ( list_of_port_declarations_ansi_item ( COMMA list_of_port_declarations_ansi_item )* )
	//     )? RPAREN;
	auto ports = make_unique<vector<unique_ptr<HdlVariableDef>>>();
	for (auto pd : ctx->nonansi_port()) {
		auto pds = visitNonansi_port(pd);
		for (auto &pd : *pds)
			ports->push_back(move(pd));
	}
	pair<HdlVariableDef*, iHdlExpr*> prev = { nullptr, nullptr };
	for (auto pdi : ctx->list_of_port_declarations_ansi_item()) {
		// list_of_port_declarations_ansi_item:
		// 	( attribute_instance )* ansi_port_declaration
		// ;
		VerAttributeParser::visitAttribute_instance(pdi->attribute_instance());
		auto apd = pdi->ansi_port_declaration();
		auto pd = visitAnsi_port_declaration(apd, prev.first, prev.second);
		prev = { pd.first.get(), pd.second };
		ports->push_back(move(pd.first));
	}
	return ports;
}

pair<unique_ptr<HdlVariableDef>, iHdlExpr*> VerPortParser::visitAnsi_port_declaration(
		sv2017Parser::Ansi_port_declarationContext *ctx,
		hdlObjects::HdlVariableDef *prev_var,
		hdlObjects::iHdlExpr *prev_var_base_t) {
	// ansi_port_declaration:
	//   ( port_direction ( net_or_var_data_type )? // net_port_header
	//     | net_or_var_data_type                   // net_port_header or variable_port_header
	//     | (identifier | KW_INTERFACE) (DOT identifier )? // interface_port_header
	//   )? port_identifier ( variable_dimension )*
	//    (ASSIGN constant_expression)?
	//   | (port_direction)? DOT port_identifier LPAREN (expression)? RPAREN
	// ;
	HdlDirection d = HdlDirection::DIR_UNKNOWN;
	if (prev_var)
		d = prev_var->direction;
	auto pd = ctx->port_direction();
	if (pd) {
		d = visitPort_direction(pd);
	}
	unique_ptr<iHdlExpr> t = nullptr;
	bool is_latched = false;
	auto nvdt = ctx->net_or_var_data_type();
	auto _pid = ctx->port_identifier()->identifier();
	auto name = VerExprParser::getIdentifierStr(_pid);
	VerExprParser ep(commentParser);
	VerTypeParser tp(commentParser);
	if (nvdt) {
		tie(t, is_latched) = tp.visitNet_or_var_data_type(nvdt);
	} else {
		if (ctx->KW_INTERFACE()) {
			t = iHdlExpr::ID("interface");
		}
		for (auto _id : ctx->identifier()) {
			auto id = VerExprParser::visitIdentifier(_id);
			t = append_expr(move(t), HdlOperatorType::DOT, move(id));
		}
	}
	if (!t and ctx->LPAREN()) {
		// | (port_direction)? DOT port_identifier LPAREN (expression)? RPAREN
		auto _e = ctx->expression();
		unique_ptr<iHdlExpr> def_val = nullptr;
		if (_e)
			def_val = ep.visitExpression(_e);
		auto p = make_unique<HdlVariableDef>(name, iHdlExpr::AUTO_T(),
				move(def_val), d, is_latched);
		return {move(p), nullptr};
	}

	if (!t) {
		if (prev_var_base_t)
			t = make_unique<iHdlExpr>(*prev_var_base_t);
		else
			t = Utils::mkWireT();
	}
	auto vds = ctx->variable_dimension();
	auto base_type = t.get();
	t = tp.applyVariable_dimension(move(t), vds);
	unique_ptr<iHdlExpr> def_val = nullptr;
	auto ce = ctx->constant_expression();
	if (ce)
		def_val = ep.visitConstant_expression(ce);
	auto p = make_unique<HdlVariableDef>(name, move(t), move(def_val), d,
			is_latched);
	return {move(p), base_type};
}
void VerPortParser::visitNonansi_port_declaration(
		sv2017Parser::Nonansi_port_declarationContext *ctx,
		std::vector<unique_ptr<HdlVariableDef>> &res) {
	// nonansi_port_declaration:
	//    ( attribute_instance )* (
	//    KW_INOUT ( net_port_type )? list_of_variable_identifiers
	//   | KW_INPUT ( net_or_var_data_type )? list_of_variable_identifiers
	//   | KW_OUTPUT ( net_or_var_data_type )? list_of_variable_port_identifiers
	//   | identifier ( DOT identifier )? list_of_variable_identifiers // identifier=interface_identifier
	//   | KW_REF ( var_data_type )? list_of_variable_identifiers
	//   )
	// ;
	string doc = commentParser.parse(ctx);
	auto attribs = ctx->attribute_instance();
	VerAttributeParser::visitAttribute_instance(attribs);
	VerTypeParser tp(commentParser);

	auto lvi = ctx->list_of_variable_identifiers();
	if (ctx->KW_INOUT()) {
		bool reg_ = false;
		auto npt = ctx->net_port_type();
		auto t = tp.visitNet_port_type(npt);
		visitList_of_variable_identifiers(lvi, move(t), reg_,
				HdlDirection::DIR_INOUT, doc, res);
	} else if (ctx->KW_INPUT()) {
		auto nvdt = ctx->net_or_var_data_type();
		auto t = tp.visitNet_or_var_data_type(nvdt);
		visitList_of_variable_identifiers(lvi, move(t.first), t.second,
				HdlDirection::DIR_IN, doc, res);
	} else if (ctx->KW_OUTPUT()) {
		auto nvdt = ctx->net_or_var_data_type();
		auto t = tp.visitNet_or_var_data_type(nvdt);
		auto lvpi = ctx->list_of_variable_port_identifiers();
		visitList_of_variable_port_identifiers(lvpi, move(t.first), t.second,
				HdlDirection::DIR_OUT, doc, res);
	} else {
		auto ids = ctx->identifier();
		if (ids.size()) {
			auto t = VerExprParser::visitIdentifier(ids[0]);
			if (ids.size() > 1) {
				assert(ids.size() == 2);
				t = make_unique<iHdlExpr>(move(t), HdlOperatorType::DOT,
						VerExprParser::visitIdentifier(ids[1]));
			}
			return visitList_of_variable_identifiers(lvi, move(t), false,
					HdlDirection::DIR_LINKAGE, doc, res);
		} else {
			auto vdt = ctx->var_data_type();
			auto t = tp.visitVar_data_type(vdt);
			return visitList_of_variable_identifiers(lvi, move(t), false,
					HdlDirection::DIR_LINKAGE, doc, res);
		}
	}
}
void VerPortParser::visitList_of_variable_port_identifiers(
		sv2017Parser::List_of_variable_port_identifiersContext *ctx,
		std::unique_ptr<iHdlExpr> base_type, bool is_latched,
		HdlDirection direction, const std::string &doc,
		std::vector<unique_ptr<HdlVariableDef>> &res) {
	// list_of_variable_port_identifiers: list_of_tf_variable_identifiers;
	auto ltvi = ctx->list_of_tf_variable_identifiers();
	visitList_of_tf_variable_identifiers(ltvi, move(base_type), is_latched,
			direction, doc, res);
}

template<typename T>
typename std::vector<T>::iterator replace_seq_with_value(std::vector<T> &vec,
		T &first_val, size_t seq_len, T replacement) {
	auto seq_start = std::find(vec.begin(), vec.end(), first_val);
	if (seq_start == vec.end())
		return vec.end(); // return last if no seq was found

	*seq_start = replacement;

	auto items_to_rm = seq_len - 1;

	// return new end
	if (items_to_rm > 0)
		return std::remove_if(std::next(seq_start), vec.end(),
				[&items_to_rm](const auto&) {
					return items_to_rm-- > 0;
				});
	return vec.end();
}

void VerPortParser::convert_non_ansi_ports_to_ansi(
		vector<unique_ptr<HdlVariableDef>> &ports,
		vector<unique_ptr<iHdlObj>> &body) {
	std::vector<unique_ptr<HdlVariableDef>> non_ansi_converted_ports(
			non_ansi_port_groups.size());
	// module x (.a(b, c)); input b, c; endmodule -> module x (input [2:0] a); wire b, c; assign {b, c} = a; endmodule
	throw ParseException("Conversion of non-ANSI ports not implemented");
	// size_t i = 0;
	// for (auto &g : non_ansi_port_groups) {
	// 	unique_ptr<iHdlExpr> lsb = iHdlExpr::INT(0), msb = nullptr;
	// 	bool _signed = false;
	// 	for (auto &p : g.second) {
	// 		// [todo] resolve width of type
	// 		if (p->type) {
    //
	// 		}
	// 	}
	// 	auto range = make_unique<iHdlExpr>(move(msb), HdlOperatorType::DOWNTO,
	// 			move(lsb));
	// 	auto nap_t = Utils::mkWireT(move(range), _signed);
	// 	auto nap = make_unique<HdlVariableDef>(g.first, move(nap_t), nullptr);
	// 	replace_seq_with_value(ports, g.second[0], g.second.size(), *nap);
	// 	non_ansi_converted_ports[i++] = move(nap);
	// }
	// auto nap = non_ansi_converted_ports.begin();
	// for (auto &g : non_ansi_port_groups) {
	// 	std::vector<unique_ptr<iHdlExpr>> ops;
	// 	for (auto &p : g.second) {
	// 		ops.push_back(iHdlExpr::ID(p->name));
	// 	}
	// 	auto conc = reduce(ops, HdlOperatorType::CONCAT);
	// 	auto this_port_id = iHdlExpr::ID(g.first);
	// 	unique_ptr<HdlStmAssign> drv;
	// 	if ((*nap)->direction == HdlDirection::DIR_IN) {
	// 		drv = make_unique<HdlStmAssign>(move(conc), move(this_port_id),
	// 				false);
	// 	} else {
	// 		drv = make_unique<HdlStmAssign>(move(this_port_id), move(conc),
	// 				false);
	// 	}
	// 	body.push_back(move(drv));
	// 	nap++;
	// }
}
void VerPortParser::visitList_of_tf_variable_identifiers(
		sv2017Parser::List_of_tf_variable_identifiersContext *ctx,
		unique_ptr<iHdlExpr> base_type, bool is_latched, HdlDirection direction,
		const std::string &doc, std::vector<unique_ptr<HdlVariableDef>> &res) {
	// list_of_tf_variable_identifiers:
	//     list_of_tf_variable_identifiers_item
	//     ( COMMA list_of_tf_variable_identifiers_item )*
	// ;
	VerTypeParser tp(commentParser);
	VerExprParser ep(commentParser);
	auto base_t_tmp = base_type.get();
	bool first = true;
	for (auto i : ctx->list_of_tf_variable_identifiers_item()) {
		// list_of_tf_variable_identifiers_item: identifier ( variable_dimension )* ( ASSIGN expression )?;
		unique_ptr<iHdlExpr> t;
		if (first)
			t = move(base_type);
		else
			t = make_unique<iHdlExpr>(*base_t_tmp);

		auto vds = i->variable_dimension();
		t = tp.applyVariable_dimension(move(t), vds);
		auto _id = i->identifier();
		auto id = VerExprParser::getIdentifierStr(_id);
		auto _e = i->expression();
		unique_ptr<iHdlExpr> def_val = nullptr;
		if (_e)
			def_val = ep.visitExpression(_e);

		auto v = make_unique<HdlVariableDef>(id, move(t), move(def_val));
		if (first) {
			first = false;
			v->__doc__ = doc;
		}
		v->is_latched = is_latched;
		v->direction = direction;
		res.push_back(move(v));
	}
}

void VerPortParser::visitList_of_variable_identifiers(
		sv2017Parser::List_of_variable_identifiersContext *ctx,
		unique_ptr<iHdlExpr> base_type, bool is_latched, HdlDirection direction,
		const std::string &doc, std::vector<unique_ptr<HdlVariableDef>> &res) {
	// list_of_variable_identifiers:
	//     list_of_variable_identifiers_item ( COMMA list_of_variable_identifiers_item )*;
	VerTypeParser tp(commentParser);
	auto base_type_tmp = base_type.get();
	bool first = true;
	for (auto i : ctx->list_of_variable_identifiers_item()) {
		// list_of_variable_identifiers_item: identifier ( variable_dimension )*;
		unique_ptr<iHdlExpr> t;
		if (first)
			t = move(base_type);
		else
			t = make_unique<iHdlExpr>(*base_type_tmp);
		auto vds = i->variable_dimension();
		t = tp.applyVariable_dimension(move(t), vds);
		auto _id = i->identifier();
		auto id = VerExprParser::getIdentifierStr(_id);
		auto v = make_unique<HdlVariableDef>(id, move(t), nullptr);
		if (first) {
			first = false;
			v->__doc__ = doc;
		}
		v->is_latched = is_latched;
		v->direction = direction;
		res.push_back(move(v));
	}
}

void VerPortParser::visitTf_port_list(sv2017Parser::Tf_port_listContext *ctx,
		std::vector<unique_ptr<HdlVariableDef>> &res) {
	// tf_port_list: tf_port_item ( COMMA tf_port_item )*;
	for (auto pl : ctx->tf_port_item()) {
		auto v = visitTf_port_item(pl);
		res.push_back(move(v));
	}
}
unique_ptr<HdlVariableDef> VerPortParser::visitTf_port_item(
		sv2017Parser::Tf_port_itemContext *ctx) {
	// tf_port_item:
	//     ( attribute_instance )* ( tf_port_direction )? ( KW_VAR )? ( data_type_or_implicit )?
	//     ( identifier ( variable_dimension )* ( ASSIGN expression )? )?;
	VerAttributeParser::visitAttribute_instance(ctx->attribute_instance());
	HdlDirection d = HdlDirection::DIR_IN;
	auto dti = ctx->data_type_or_implicit();
	VerTypeParser tp(commentParser);
	auto t = tp.visitData_type_or_implicit(dti, nullptr);
	auto vds = ctx->variable_dimension();
	t = tp.applyVariable_dimension(move(t), vds);
	VerExprParser ep(commentParser);
	string name = "";
	auto id = ctx->identifier();
	if (id) {
		name = ep.getIdentifierStr(id);
	}
	unique_ptr<iHdlExpr> v = nullptr;
	auto e = ctx->expression();
	if (e) {
		v = ep.visitExpression(e);
	}

	bool is_latched = ctx->KW_VAR() != nullptr;
	return make_unique<HdlVariableDef>(name, move(t), move(v), d, is_latched);
}

HdlDirection VerPortParser::visitPort_direction(
		sv2017Parser::Port_directionContext *ctx) {
	// port_direction:
	//     KW_INPUT
	//     | KW_OUTPUT
	//     | KW_INOUT
	//     | KW_REF
	// ;
	if (ctx->KW_INPUT())
		return HdlDirection::DIR_IN;
	if (ctx->KW_OUTPUT())
		return HdlDirection::DIR_OUT;
	if (ctx->KW_INOUT())
		return HdlDirection::DIR_INOUT;
	assert(ctx->KW_REF());
	return HdlDirection::DIR_LINKAGE;
}

HdlDirection VerPortParser::visitTf_port_direction(
		sv2017Parser::Tf_port_directionContext *ctx) {
	// tf_port_direction:
	//     KW_CONST KW_REF
	//     | port_direction
	// ;
	if (ctx->KW_REF()) {
		return HdlDirection::DIR_LINKAGE;
	} else {
		auto pd = ctx->port_direction();
		assert(pd);
		return visitPort_direction(pd);
	}
}

}
}
