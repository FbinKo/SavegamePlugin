// Fill out your copyright notice in the Description page of Project Settings.


#include "K2Node_GetRecords.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "CustomSaveGame.h"

#define LOCTEXT_NAMESPACE "K2Node_GetRecords"

namespace RecordsNameHelper
{
	const FName RecordTypeName = "Type";
	const FName KeyPinName = "Key";
}

UK2Node_GetRecords::UK2Node_GetRecords(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	//NodeTooltip = LOCTEXT("NodeTooltip", "Attempts to retrieve a TableRow from a DataTable via it's RowName");
}

void UK2Node_GetRecords::AllocateDefaultPins()
{
	// type pin
	/*UEdGraphPin* RecordTypePin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Enum, StaticEnum<ERecordType>(), RecordsNameHelper::RecordTypeName);
	SetPinToolTip(*RecordTypePin, LOCTEXT("RecordTypePinDescription", "The record type you want to retrieve"));*/

	// key pin
	UEdGraphPin* KeyPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Name, RecordsNameHelper::KeyPinName);
	SetPinToolTip(*KeyPin, LOCTEXT("KeyPinDescription", "The key used for which records to access"));

	UEdGraphPin* ResultPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, UEdGraphSchema_K2::PN_ReturnValue);
	ResultPin->PinFriendlyName = LOCTEXT("GetDataTableRow Output Row", "Out Row");
	SetPinToolTip(*ResultPin, LOCTEXT("ResultPinDescription", "The returned TableRow, if found"));

	Super::AllocateDefaultPins();
}

void UK2Node_GetRecords::SetPinToolTip(UEdGraphPin& MutatablePin, const FText& PinDescription) const
{
	MutatablePin.PinToolTip = UEdGraphSchema_K2::TypeToText(MutatablePin.PinType).ToString();

	UEdGraphSchema_K2 const* const K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
	if (K2Schema != nullptr)
	{
		MutatablePin.PinToolTip += TEXT(" ");
		MutatablePin.PinToolTip += K2Schema->GetPinDisplayName(&MutatablePin).ToString();
	}

	MutatablePin.PinToolTip += FString(TEXT("\n")) + PinDescription.ToString();
}

void UK2Node_GetRecords::RefreshOutputPinType()
{
	UScriptStruct* OutputType = GetRecordsStructType();
	SetReturnTypeForStruct(OutputType);
}

void UK2Node_GetRecords::SetReturnTypeForStruct(UScriptStruct* InClass)
{
	UScriptStruct* OldStruct = GetReturnTypeForStruct();
	if (InClass != OldStruct)
	{
		UEdGraphPin* ResultPin = GetResultPin();

		if (ResultPin->SubPins.Num() > 0)
		{
			GetSchema()->RecombinePin(ResultPin);
		}

		// NOTE: purposefully not disconnecting the ResultPin (even though it changed type)... we want the user to see the old
		//       connections, and incompatible connections will produce an error (plus, some super-struct connections may still be valid)
		ResultPin->PinType.PinSubCategoryObject = InClass;
		ResultPin->PinType.PinCategory = (InClass == nullptr) ? UEdGraphSchema_K2::PC_Wildcard : UEdGraphSchema_K2::PC_Struct;

		CachedNodeTitle.Clear();
	}
}

FText UK2Node_GetRecords::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	//if (TitleType == ENodeTitleType::MenuTitle)
	//{
	//	return LOCTEXT("ListViewTitle", "Get Records");
	//}
	//else if (UEdGraphPin* RecordTypePin = GetRecordTypePin())
	//{
	//	if (RecordTypePin->LinkedTo.Num() > 0)
	//	{
	//		return NSLOCTEXT("K2Node", "Records_Title_Unknown", "Get Records");
	//	}
	//	else if (RecordTypePin->DefaultObject == nullptr)
	//	{
	//		return NSLOCTEXT("K2Node", "Records_Title_None", "Get NONE Records");
	//	}
	//	else if (CachedNodeTitle.IsOutOfDate(this))
	//	{
	//		FFormatNamedArguments Args;
	//		Args.Add(TEXT("RecordsName"), FText::FromString(RecordTypePin->DefaultObject->GetName()));

	//		FText LocFormat = NSLOCTEXT("K2Node", "Records", "Get {RecordsName} Records");
	//		// FText::Format() is slow, so we cache this to save on performance
	//		CachedNodeTitle.SetCachedText(FText::Format(LocFormat, Args), this);
	//	}
	//}
	//else
	//{
	//	return NSLOCTEXT("K2Node", "Records_Title_None", "Get NONE Records");
	//}
	return CachedNodeTitle;
}

void UK2Node_GetRecords::PinDefaultValueChanged(UEdGraphPin* Pin)
{
}

FText UK2Node_GetRecords::GetTooltipText() const
{
	return NodeTooltip;
}

void UK2Node_GetRecords::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
}

FSlateIcon UK2Node_GetRecords::GetIconAndTint(FLinearColor& OutColor) const
{
	return FSlateIcon();
}

void UK2Node_GetRecords::PostReconstructNode()
{
	Super::PostReconstructNode();

	RefreshOutputPinType();
}

void UK2Node_GetRecords::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	Super::ReallocatePinsDuringReconstruction(OldPins);

	//if (UEdGraphPin* DataTablePin = GetDataTablePin(&OldPins))
	//{
	//	if (UDataTable* DataTable = Cast<UDataTable>(DataTablePin->DefaultObject))
	//	{
	//		// make sure to properly load the data-table object so that we can 
	//		// farm the "RowStruct" property from it (below, in GetDataTableRowStructType)
	//		PreloadObject(DataTable);
	//	}
	//}
}

void UK2Node_GetRecords::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
}

FText UK2Node_GetRecords::GetMenuCategory() const
{
	return FText(LOCTEXT("RecordsMenu", "Records"));
}

bool UK2Node_GetRecords::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin == GetResultPin() && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		bool bDisallowed = true;
		if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
		{
			if (UScriptStruct* ConnectionType = Cast<UScriptStruct>(OtherPin->PinType.PinSubCategoryObject.Get()))
			{
				//bDisallowed = !FDataTableEditorUtils::IsValidTableStruct(ConnectionType);
			}
		}
		else if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			bDisallowed = false;
		}

		if (bDisallowed)
		{
			OutReason = TEXT("Must be a struct that can be used in a DataTable");
		}
		return bDisallowed;
	}
	return false;
}

void UK2Node_GetRecords::EarlyValidation(FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	/*const UEdGraphPin* DataTablePin = GetDataTablePin();
	const UEdGraphPin* RowNamePin = GetRowNamePin();
	if (!DataTablePin || !RowNamePin)
	{
		MessageLog.Error(*LOCTEXT("MissingPins", "Missing pins in @@").ToString(), this);
		return;
	}

	if (DataTablePin->LinkedTo.Num() == 0)
	{
		const UDataTable* DataTable = Cast<UDataTable>(DataTablePin->DefaultObject);
		if (!DataTable)
		{
			MessageLog.Error(*LOCTEXT("NoDataTable", "No DataTable in @@").ToString(), this);
			return;
		}

		if (!RowNamePin->LinkedTo.Num())
		{
			const FName CurrentName = FName(*RowNamePin->GetDefaultAsString());
			if (!DataTable->GetRowNames().Contains(CurrentName))
			{
				const FString Msg = FText::Format(
					LOCTEXT("WrongRowNameFmt", "'{0}' row name is not stored in '{1}'. @@"),
					FText::FromString(CurrentName.ToString()),
					FText::FromString(GetFullNameSafe(DataTable))
				).ToString();
				MessageLog.Error(*Msg, this);
				return;
			}
		}
	}*/
}

void UK2Node_GetRecords::PreloadRequiredAssets()
{
	//if (UEdGraphPin* DataTablePin = GetDataTablePin())
	//{
	//	if (UDataTable* DataTable = Cast<UDataTable>(DataTablePin->DefaultObject))
	//	{
	//		// make sure to properly load the data-table object so that we can 
	//		// farm the "RowStruct" property from it (below, in GetDataTableRowStructType)
	//		PreloadObject(DataTable);
	//	}
	//}
	return Super::PreloadRequiredAssets();
}

void UK2Node_GetRecords::NotifyPinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::NotifyPinConnectionListChanged(Pin);
}

UScriptStruct* UK2Node_GetRecords::GetReturnTypeForStruct()
{
	UScriptStruct* ReturnStructType = (UScriptStruct*)(GetResultPin()->PinType.PinSubCategoryObject.Get());

	return ReturnStructType;
}

UEdGraphPin* UK2Node_GetRecords::GetResultPin() const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

	UEdGraphPin* Pin = FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue);
	check(Pin->Direction == EGPD_Output);
	return Pin;
}

UScriptStruct* UK2Node_GetRecords::GetRecordsStructType() const
{
	UScriptStruct* RowStructType = FRecords::StaticStruct();//nullptr;

	UEdGraphPin* TypePin = FindPinChecked(RecordsNameHelper::RecordTypeName);
	RowStructType = Cast<UScriptStruct>(TypePin->PinType.PinSubCategoryObject.Get());
	if (RowStructType) {
		//if()
	}
	//if (DataTablePin && DataTablePin->DefaultObject != nullptr && DataTablePin->LinkedTo.Num() == 0)
	//{
	//	if (const UDataTable* DataTable = Cast<const UDataTable>(DataTablePin->DefaultObject))
	//	{
	//		RowStructType = DataTable->RowStruct;
	//	}
	//}

	//if (RowStructType == nullptr)
	//{
	//	UEdGraphPin* ResultPin = GetResultPin();
	//	if (ResultPin && ResultPin->LinkedTo.Num() > 0)
	//	{
	//		RowStructType = Cast<UScriptStruct>(ResultPin->LinkedTo[0]->PinType.PinSubCategoryObject.Get());
	//		if (RowStructType == nullptr && ResultPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
	//		{
	//			RowStructType = GetFallbackStruct();
	//		}
	//		for (int32 LinkIndex = 1; LinkIndex < ResultPin->LinkedTo.Num(); ++LinkIndex)
	//		{
	//			UEdGraphPin* Link = ResultPin->LinkedTo[LinkIndex];
	//			UScriptStruct* LinkType = Cast<UScriptStruct>(Link->PinType.PinSubCategoryObject.Get());

	//			if (RowStructType && RowStructType->IsChildOf(LinkType))
	//			{
	//				RowStructType = LinkType;
	//			}
	//		}
	//	}
	//}
	return RowStructType;
}

#undef LOCTEXT_NAMESPACE
