#include "SmvFlightLink.h"

// ---- ortak taban yardımcıları (SmvFlightLink.h) ----
void USmvFlightLink::EmitEvent(const FString& Text, int32 Severity)
{
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("t"), TEXT("event"));
	M->SetStringField(TEXT("text"), Text);
	M->SetNumberField(TEXT("severity"), Severity);
	M->SetNumberField(TEXT("ts"), Now());
	EmitMessage(M);
}

void USmvFlightLink::EmitState(const FString& State)
{
	FlightInfo.State = State;
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("t"), TEXT("state"));
	M->SetStringField(TEXT("state"), State);
	EmitMessage(M);
}

void USmvFlightLink::EmitResult(const FString& Rid, const FString& Name, bool bOk, const FString& Error)
{
	TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
	M->SetStringField(TEXT("t"), TEXT("res"));
	M->SetStringField(TEXT("rid"), Rid);
	M->SetStringField(TEXT("name"), Name);
	M->SetBoolField(TEXT("ok"), bOk);
	if (!Error.IsEmpty()) M->SetStringField(TEXT("error"), Error);
	EmitMessage(M);
}

