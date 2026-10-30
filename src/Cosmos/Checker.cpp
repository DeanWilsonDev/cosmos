#include "Cosmos/Checker.h"

#include "Cosmos/Properties.h"
#include "ResolverDetail.h"

#include <algorithm>
#include <tuple>

namespace Cosmos {

namespace {

SourceLocation Advanced(SourceLocation Location, std::size_t Columns) {
    Location.Column += static_cast<std::uint32_t>(Columns);
    return Location;
}

class StylesheetChecker {
public:
    StylesheetChecker(const StylesheetSet& Sheets, bool CheckVariables)
        : Scope_(Detail::BuildScope(Sheets)), CheckVariables_(CheckVariables) {
        for (const auto& [Name, Value] : Scope_.Values) {
            VariableNames_.push_back(Name);
        }
        std::ranges::sort(VariableNames_);
    }

    void CheckSheet(const Stylesheet& Sheet) {
        if (Sheet.Root) {
            for (const VariableDeclaration& Variable : Sheet.Root->Variables) {
                CheckVariableValue(Variable.Value);
            }
        }
        for (const RulePtr& R : Sheet.Rules) {
            CheckRule(*R);
        }
        std::ranges::stable_sort(Diagnostics_, [](const CheckDiagnostic& A, const CheckDiagnostic& B) {
            return std::tie(A.Location.Line, A.Location.Column) < std::tie(B.Location.Line, B.Location.Column);
        });
    }

    std::vector<CheckDiagnostic> TakeDiagnostics() { return std::move(Diagnostics_); }

private:
    void CheckRule(const Rule& R) {
        const std::string_view Tag =
            R.Kind == SelectorKind::Primitive ? PrimitiveTagForSelector(R.PrimitiveName) : std::string_view{};
        for (const VariableDeclaration& Variable : R.Variables) {
            CheckVariableValue(Variable.Value);
        }
        for (const Declaration& Decl : R.Declarations) {
            CheckDeclaration(Decl, Tag);
        }
        if (!R.StartingDeclarations.empty() && R.Pseudo) {
            Diagnostics_.push_back(CheckDiagnostic{CheckSeverity::Error, Detail::StartingStyleInPseudoMessage(*R.Pseudo),
                                                   R.Location, R.Location, {}});
        } else {
            for (const Declaration& Decl : R.StartingDeclarations) {
                CheckDeclaration(Decl, Tag);
            }
        }
        for (const RulePtr& Nested : R.NestedRules) {
            CheckRule(*Nested);
        }
    }

    void CheckDeclaration(const Declaration& Decl, std::string_view Tag) {
        if (!FindProperty(Decl.Property)) {
            ReportUnknownProperty(Decl);
            return;
        }
        bool AnyMissing = false;
        for (const ValuePart& Part : Decl.Values) {
            const Detail::VariableLookup Lookup = Detail::LookUpVariable(Part, Scope_);
            if (!Lookup.Missing.empty()) {
                AnyMissing = true;
                ReportUndefinedVariable(Part, Lookup);
            }
        }
        if (AnyMissing) {
            return;
        }
        std::vector<ResolveDiagnostic> Found;
        Detail::ApplyOneDeclaration(Decl, Scope_, Tag, Found);
        for (ResolveDiagnostic& Diagnostic : Found) {
            Diagnostics_.push_back(CheckDiagnostic{CheckSeverity::Error, std::move(Diagnostic.Message),
                                                   Diagnostic.Location, Diagnostic.End,
                                                   std::move(Diagnostic.Suggestion)});
        }
    }

    void CheckVariableValue(const ValuePart& Value) {
        const Detail::VariableLookup Lookup = Detail::LookUpVariable(Value, Scope_);
        if (!Lookup.Missing.empty()) {
            ReportUndefinedVariable(Value, Lookup);
        } else if (!Lookup.Cycle.empty()) {
            Diagnostics_.push_back(CheckDiagnostic{CheckSeverity::Error, Detail::VariableCycleMessage(Lookup.Cycle),
                                                   Value.Location, Value.End, {}});
        }
    }

    void ReportUnknownProperty(const Declaration& Decl) {
        static const std::vector<std::string_view> Names = PropertyNames();
        CheckDiagnostic Diagnostic{CheckSeverity::Warning,
                                   "Unknown property `" + Decl.Property + "` -- Cosmos ignores it.", Decl.Location,
                                   Advanced(Decl.Location, Decl.Property.size()), {}};
        if (const std::string_view Fix = ClosestMatch(Decl.Property, Names); !Fix.empty()) {
            Diagnostic.Message += " Did you mean `" + std::string(Fix) + "`?";
            Diagnostic.Suggestion = Fix;
        }
        Diagnostics_.push_back(std::move(Diagnostic));
    }

    // Reported at Part, the value written in this file, even when the missing name is
    // reached through a variable declared in another file.
    void ReportUndefinedVariable(const ValuePart& Part, const Detail::VariableLookup& Lookup) {
        if (!CheckVariables_) {
            return;
        }
        CheckDiagnostic Diagnostic{CheckSeverity::Error, Detail::UndefinedVariableMessage(Lookup.Missing),
                                   Part.Location, Part.End, {}};
        if (Lookup.MissingAt != &Part) {
            Diagnostic.Message += " It's reached through `var(--" + *Part.VariableRef + ")`.";
            Diagnostics_.push_back(std::move(Diagnostic));
            return;
        }
        const std::vector<std::string_view> Candidates(VariableNames_.begin(), VariableNames_.end());
        if (const std::string_view Fix = ClosestMatch(Lookup.Missing, Candidates); !Fix.empty()) {
            Diagnostic.Message += " Did you mean `--" + std::string(Fix) + "`?";
            Diagnostic.Suggestion = "var(--" + std::string(Fix) + ")";
        }
        Diagnostics_.push_back(std::move(Diagnostic));
    }

    VariableScope                Scope_;
    bool                         CheckVariables_;
    std::vector<std::string>     VariableNames_;
    std::vector<CheckDiagnostic> Diagnostics_;
};

} // namespace

std::vector<CheckDiagnostic> CheckStylesheet(const StylesheetSet& Sheets) {
    const Stylesheet* Checked = Sheets.Component ? Sheets.Component : Sheets.Global;
    if (!Checked) {
        return {};
    }
    StylesheetChecker Checker(Sheets, Sheets.Global != nullptr);
    Checker.CheckSheet(*Checked);
    return Checker.TakeDiagnostics();
}

} // namespace Cosmos
