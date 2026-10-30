#include "AstJson.h"

#include <string>

namespace CosmosCli {

namespace {

using Json = Amanuensis::Json;
using Amanuensis::JsonValue;

std::string LineColumn(const Cosmos::SourceLocation& Location) {
    return std::to_string(Location.Line) + ":" + std::to_string(Location.Column);
}

void AddLocation(JsonValue& Node, const Cosmos::SourceLocation& Location, bool Locations) {
    if (Locations) {
        Json::Insert(Node, "location", JsonValue(LineColumn(Location)));
    }
}

void AddEnd(JsonValue& Node, const Cosmos::SourceLocation& End, bool Locations) {
    if (Locations && End.Line > 0) {
        Json::Insert(Node, "end", JsonValue(LineColumn(End)));
    }
}

JsonValue ValueToJson(const Cosmos::ValuePart& Part, bool Locations) {
    JsonValue Node = Json::MakeObject();
    auto      Set  = [&](const char* Kind, const std::string& Text) {
        Json::Insert(Node, "kind", JsonValue(Kind));
        Json::Insert(Node, "text", JsonValue(Text));
    };
    if (Part.Literal) {
        Set("keyword", *Part.Literal);
        if (Part.CallArgument) {
            Json::Insert(Node, "argument", JsonValue(*Part.CallArgument));
        }
    } else if (Part.Number) {
        Set("number", *Part.Number);
    } else if (Part.StringValue) {
        Set("string", *Part.StringValue);
    } else if (Part.HexColor) {
        Set("hex", *Part.HexColor);
    } else if (Part.VariableRef) {
        Set("var", *Part.VariableRef);
    } else if (Part.AttrRef) {
        Set("attr", *Part.AttrRef);
    } else if (Part.ColorChannels) {
        Json::Insert(Node, "kind", JsonValue(Part.ColorChannels->size() == 4 ? "rgba" : "rgb"));
        JsonValue Channels = Json::MakeArray();
        for (const Cosmos::ColorChannelPart& Channel : *Part.ColorChannels) {
            JsonValue Each = Json::MakeObject();
            if (Channel.AttrRef) {
                Json::Insert(Each, "attr", JsonValue(*Channel.AttrRef));
            } else {
                Json::Insert(Each, "number", JsonValue(Channel.Number.value_or("")));
            }
            Json::PushBack(Channels, std::move(Each));
        }
        Json::Insert(Node, "channels", std::move(Channels));
    }
    if (Part.FollowsComma) {
        Json::Insert(Node, "followsComma", JsonValue(true));
    }
    AddLocation(Node, Part.Location, Locations);
    AddEnd(Node, Part.End, Locations);
    return Node;
}

JsonValue DeclarationsToJson(const std::vector<Cosmos::Declaration>& Declarations, bool Locations) {
    JsonValue List = Json::MakeArray();
    for (const Cosmos::Declaration& Decl : Declarations) {
        JsonValue Node = Json::MakeObject();
        Json::Insert(Node, "property", JsonValue(Decl.Property));
        JsonValue Values = Json::MakeArray();
        for (const Cosmos::ValuePart& Part : Decl.Values) {
            Json::PushBack(Values, ValueToJson(Part, Locations));
        }
        Json::Insert(Node, "values", std::move(Values));
        AddLocation(Node, Decl.Location, Locations);
        AddEnd(Node, Decl.End, Locations);
        Json::PushBack(List, std::move(Node));
    }
    return List;
}

JsonValue VariablesToJson(const std::vector<Cosmos::VariableDeclaration>& Variables, bool Locations) {
    JsonValue List = Json::MakeArray();
    for (const Cosmos::VariableDeclaration& Variable : Variables) {
        JsonValue Node = Json::MakeObject();
        Json::Insert(Node, "name", JsonValue("--" + Variable.Name));
        Json::Insert(Node, "value", ValueToJson(Variable.Value, Locations));
        AddLocation(Node, Variable.Location, Locations);
        Json::PushBack(List, std::move(Node));
    }
    return List;
}

const char* PseudoName(Cosmos::PseudoKind Kind) {
    switch (Kind) {
    case Cosmos::PseudoKind::Hover: return "hover";
    case Cosmos::PseudoKind::Active: return "active";
    case Cosmos::PseudoKind::Disabled: return "disabled";
    }
    return "";
}

JsonValue RuleToJson(const Cosmos::Rule& R, bool Locations) {
    JsonValue Node = Json::MakeObject();
    if (R.Kind == Cosmos::SelectorKind::Class) {
        Json::Insert(Node, "class", JsonValue(R.ClassName));
    } else {
        Json::Insert(Node, "element", JsonValue(R.PrimitiveName));
    }
    if (R.Pseudo) {
        Json::Insert(Node, "pseudo", JsonValue(PseudoName(*R.Pseudo)));
    }
    AddLocation(Node, R.Location, Locations);
    AddEnd(Node, R.End, Locations);
    if (!R.Variables.empty()) {
        Json::Insert(Node, "variables", VariablesToJson(R.Variables, Locations));
    }
    Json::Insert(Node, "declarations", DeclarationsToJson(R.Declarations, Locations));
    if (!R.StartingDeclarations.empty()) {
        Json::Insert(Node, "startingStyle", DeclarationsToJson(R.StartingDeclarations, Locations));
    }
    if (!R.NestedRules.empty()) {
        JsonValue Nested = Json::MakeArray();
        for (const Cosmos::RulePtr& Child : R.NestedRules) {
            Json::PushBack(Nested, RuleToJson(*Child, Locations));
        }
        Json::Insert(Node, "rules", std::move(Nested));
    }
    return Node;
}

} // namespace

JsonValue StylesheetToJson(const Cosmos::Stylesheet& Sheet, bool Locations) {
    JsonValue Node = Json::MakeObject();
    if (Sheet.Root) {
        JsonValue Root = Json::MakeObject();
        AddLocation(Root, Sheet.Root->Location, Locations);
        AddEnd(Root, Sheet.Root->End, Locations);
        Json::Insert(Root, "variables", VariablesToJson(Sheet.Root->Variables, Locations));
        Json::Insert(Node, "root", std::move(Root));
    }
    JsonValue Rules = Json::MakeArray();
    for (const Cosmos::RulePtr& R : Sheet.Rules) {
        Json::PushBack(Rules, RuleToJson(*R, Locations));
    }
    Json::Insert(Node, "rules", std::move(Rules));
    return Node;
}

} // namespace CosmosCli
