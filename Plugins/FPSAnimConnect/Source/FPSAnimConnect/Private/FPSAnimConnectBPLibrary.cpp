#include "FPSAnimConnectBPLibrary.h"

#include "Animation/AnimBlueprint.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimSequence.h"
#include "AnimationGraph.h"
#include "AnimationStateMachineGraph.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_SequencePlayer.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_StateResult.h"
#include "AnimGraphNode_TransitionResult.h"
#include "AnimStateEntryNode.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "Engine/EngineBaseTypes.h"
#include "K2Node_CallFunction.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_InputActionEvent.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogFPSAnimConnect, Log, All);

namespace FPSAnimConnectLocal
{
	UEdGraph* FindAnimGraph(UAnimBlueprint* AnimBP)
	{
		TArray<UEdGraph*> Graphs;
		AnimBP->GetAllGraphs(Graphs);
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
			{
				return Graph;
			}
		}
		for (UEdGraph* Graph : Graphs)
		{
			if (Graph && Graph->IsA<UAnimationGraph>())
			{
				return Graph;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindPoseOutputPin(UAnimGraphNode_Base* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == TEXT("struct"))
			{
				return Pin;
			}
		}
		return Node->FindPin(TEXT("Pose"), EGPD_Output);
	}

	UEdGraphPin* FindResultInputPin(UAnimGraphNode_Base* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		if (UEdGraphPin* ResultPin = Node->FindPin(TEXT("Result"), EGPD_Input))
		{
			return ResultPin;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == TEXT("struct"))
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindDataInputPin(UEdGraphNode* Node, FName PreferredName = NAME_None)
	{
		if (!Node)
		{
			return nullptr;
		}
		if (PreferredName != NAME_None)
		{
			if (UEdGraphPin* Named = Node->FindPin(PreferredName, EGPD_Input))
			{
				return Named;
			}
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input
				&& Pin->PinName != UEdGraphSchema_K2::PN_Execute
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != UEdGraphSchema_K2::PN_Self)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindDataOutputPin(UEdGraphNode* Node)
	{
		if (!Node)
		{
			return nullptr;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != UEdGraphSchema_K2::PN_Then)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	UEdGraphPin* FindTransitionBoolPin(UAnimGraphNode_TransitionResult* ResultNode)
	{
		if (!ResultNode)
		{
			return nullptr;
		}
		if (UEdGraphPin* BoolIn = ResultNode->FindPin(TEXT("bCanEnterTransition"), EGPD_Input))
		{
			return BoolIn;
		}
		for (UEdGraphPin* Pin : ResultNode->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	bool TryLink(UEdGraphPin* From, UEdGraphPin* To)
	{
		if (!From || !To)
		{
			return false;
		}
		To->BreakAllPinLinks(true);
		const UEdGraphSchema* Schema = From->GetOwningNode()->GetGraph()->GetSchema();
		if (Schema && Schema->TryCreateConnection(From, To))
		{
			return To->LinkedTo.Num() > 0;
		}
		From->MakeLinkTo(To);
		return To->LinkedTo.Num() > 0;
	}

	bool SaveAssetPackage(UObject* Asset)
	{
		UPackage* Package = Asset->GetOutermost();
		Package->MarkPackageDirty();
		const FString PackageFileName = FPackageName::LongPackageNameToFilename(
			Package->GetName(),
			FPackageName::GetAssetPackageExtension());
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		SaveArgs.Error = GError;
		SaveArgs.SaveFlags = SAVE_NoError;
		return UPackage::SavePackage(Package, Asset, *PackageFileName, SaveArgs);
	}

	UAnimGraphNode_SequencePlayer* EnsureSequencePlayer(
		UEdGraph* Graph,
		UAnimSequence* Sequence,
		int32 PosX,
		int32 PosY)
	{
		UAnimGraphNode_SequencePlayer* Player = nullptr;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimGraphNode_SequencePlayer* Existing = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				Player = Existing;
				break;
			}
		}

		if (!Player)
		{
			FGraphNodeCreator<UAnimGraphNode_SequencePlayer> Creator(*Graph);
			Player = Creator.CreateNode();
			Player->NodePosX = PosX;
			Player->NodePosY = PosY;
			Creator.Finalize();
		}

		Player->SetAnimationAsset(Sequence);
		Player->Node.SetSequence(Sequence);
		Player->Node.SetLoopAnimation(true);
		Player->ReconstructNode();
		return Player;
	}

	bool WireStateAnimation(UAnimStateNode* State, UAnimSequence* Sequence)
	{
		if (!State || !State->BoundGraph || !Sequence)
		{
			return false;
		}

		UEdGraph* StateGraph = State->BoundGraph;
		UAnimGraphNode_StateResult* StateResult = State->GetResultNodeInsideState();
		if (!StateResult)
		{
			for (UEdGraphNode* Node : StateGraph->Nodes)
			{
				if (UAnimGraphNode_StateResult* Result = Cast<UAnimGraphNode_StateResult>(Node))
				{
					StateResult = Result;
					break;
				}
			}
		}
		if (!StateResult)
		{
			return false;
		}

		UAnimGraphNode_SequencePlayer* Player = EnsureSequencePlayer(
			StateGraph,
			Sequence,
			StateResult->NodePosX - 350,
			StateResult->NodePosY);
		return TryLink(FindPoseOutputPin(Player), FindResultInputPin(StateResult));
	}

	FName EnsureIsMovingVariable(UAnimBlueprint* AnimBP)
	{
		static const FName IsMovingName(TEXT("IsMoving"));
		for (const FBPVariableDescription& Var : AnimBP->NewVariables)
		{
			if (Var.VarName == IsMovingName)
			{
				return IsMovingName;
			}
		}

		FEdGraphPinType BoolType;
		BoolType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
		FBlueprintEditorUtils::AddMemberVariable(AnimBP, IsMovingName, BoolType);
		UE_LOG(LogFPSAnimConnect, Display, TEXT("Created IsMoving variable"));
		return IsMovingName;
	}

	void RemoveUnusedSpeedVariable(UAnimBlueprint* AnimBP)
	{
		static const FName SpeedName(TEXT("Speed"));
		for (const FBPVariableDescription& Var : AnimBP->NewVariables)
		{
			if (Var.VarName == SpeedName)
			{
				FBlueprintEditorUtils::RemoveMemberVariable(AnimBP, SpeedName);
				UE_LOG(LogFPSAnimConnect, Display, TEXT("Removed unused Speed variable"));
				return;
			}
		}
	}

	UK2Node_CallFunction* CreateCall(
		UEdGraph* Graph,
		UClass* Class,
		FName FunctionName,
		int32 X,
		int32 Y)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		UK2Node_CallFunction* Node = Creator.CreateNode();
		Node->FunctionReference.SetExternalMember(FunctionName, Class);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		Node->ReconstructNode();
		return Node;
	}

	UK2Node_VariableSet* CreateVariableSet(UEdGraph* Graph, FName VarName, int32 X, int32 Y)
	{
		FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
		UK2Node_VariableSet* Node = Creator.CreateNode();
		Node->VariableReference.SetSelfMember(VarName);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		Node->AllocateDefaultPins();
		Node->ReconstructNode();
		return Node;
	}

	void DeleteNodeSafe(UEdGraph* Graph, UEdGraphNode* Node)
	{
		if (!Graph || !Node)
		{
			return;
		}
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	}

	/**
	 * Rebuild BlueprintUpdateAnimation:
	 * TryGetPawnOwner -> IsValid -> Branch
	 *   True:  GetVelocity -> VSize -> Greater(3) -> Set IsMoving
	 *   False: Set IsMoving = false
	 */
	bool RebuildIsMovingEventGraph(UAnimBlueprint* AnimBP, FName IsMovingVar)
	{
		if (AnimBP->UbergraphPages.Num() == 0)
		{
			return false;
		}
		UEdGraph* Graph = AnimBP->UbergraphPages[0];
		if (!Graph)
		{
			return false;
		}

		UK2Node_Event* UpdateEvent = nullptr;
		TArray<UEdGraphNode*> ToDelete;

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
			{
				if (EventNode->EventReference.GetMemberName() == TEXT("BlueprintUpdateAnimation"))
				{
					UpdateEvent = EventNode;
				}
			}

			if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
			{
				const FName Fn = Call->FunctionReference.GetMemberName();
				if (Fn == TEXT("TryGetPawnOwner")
					|| Fn == TEXT("GetVelocity")
					|| Fn == TEXT("VSize")
					|| Fn == TEXT("Vector_Distance")
					|| Fn == TEXT("IsValid")
					|| Fn == TEXT("Greater_FloatFloat")
					|| Fn == TEXT("Greater_DoubleDouble")
					|| Fn == TEXT("LessEqual_FloatFloat")
					|| Fn == TEXT("LessEqual_DoubleDouble")
					|| Fn == TEXT("Not_PreBool"))
				{
					ToDelete.Add(Node);
				}
			}
			else if (UK2Node_IfThenElse* Branch = Cast<UK2Node_IfThenElse>(Node))
			{
				ToDelete.Add(Branch);
			}
			else if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
			{
				const FName VarName = SetNode->GetVarName();
				if (VarName == IsMovingVar || VarName == TEXT("Speed"))
				{
					ToDelete.Add(SetNode);
				}
			}
		}

		for (UEdGraphNode* Node : ToDelete)
		{
			DeleteNodeSafe(Graph, Node);
		}

		if (!UpdateEvent)
		{
			UE_LOG(LogFPSAnimConnect, Error, TEXT("BlueprintUpdateAnimation event not found"));
			return false;
		}

		if (UEdGraphPin* ThenPin = UpdateEvent->FindPin(UEdGraphSchema_K2::PN_Then))
		{
			ThenPin->BreakAllPinLinks(true);
		}

		const int32 BaseX = UpdateEvent->NodePosX + 250;
		const int32 BaseY = UpdateEvent->NodePosY;

		UK2Node_CallFunction* TryGetPawn = CreateCall(
			Graph, UAnimInstance::StaticClass(), TEXT("TryGetPawnOwner"), BaseX, BaseY);

		UK2Node_CallFunction* IsValidNode = CreateCall(
			Graph, UKismetSystemLibrary::StaticClass(), TEXT("IsValid"), BaseX + 250, BaseY);

		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
		UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
		Branch->NodePosX = BaseX + 500;
		Branch->NodePosY = BaseY;
		BranchCreator.Finalize();

		UK2Node_CallFunction* GetVelocity = CreateCall(
			Graph, AActor::StaticClass(), TEXT("GetVelocity"), BaseX + 750, BaseY - 120);

		UK2Node_CallFunction* VSize = CreateCall(
			Graph, UKismetMathLibrary::StaticClass(), TEXT("VSize"), BaseX + 1000, BaseY - 120);

		FName GreaterName = TEXT("Greater_DoubleDouble");
		if (!UKismetMathLibrary::StaticClass()->FindFunctionByName(GreaterName))
		{
			GreaterName = TEXT("Greater_FloatFloat");
		}
		UK2Node_CallFunction* Greater = CreateCall(
			Graph, UKismetMathLibrary::StaticClass(), GreaterName, BaseX + 1250, BaseY - 120);

		UK2Node_VariableSet* SetMovingTruePath = CreateVariableSet(
			Graph, IsMovingVar, BaseX + 1500, BaseY - 120);
		UK2Node_VariableSet* SetMovingFalsePath = CreateVariableSet(
			Graph, IsMovingVar, BaseX + 750, BaseY + 140);

		// Data links
		TryLink(TryGetPawn->GetReturnValuePin(), IsValidNode->FindPinChecked(TEXT("Object")));
		TryLink(IsValidNode->GetReturnValuePin(), Branch->GetConditionPin());

		if (UEdGraphPin* SelfPin = GetVelocity->FindPin(UEdGraphSchema_K2::PN_Self))
		{
			TryLink(TryGetPawn->GetReturnValuePin(), SelfPin);
		}

		TryLink(GetVelocity->GetReturnValuePin(), FindDataInputPin(VSize, TEXT("A")));
		TryLink(VSize->GetReturnValuePin(), FindDataInputPin(Greater, TEXT("A")));
		if (UEdGraphPin* BPin = Greater->FindPin(TEXT("B")))
		{
			BPin->DefaultValue = TEXT("3.0");
		}

		TryLink(Greater->GetReturnValuePin(), FindDataInputPin(SetMovingTruePath, IsMovingVar));

		if (UEdGraphPin* FalseValuePin = FindDataInputPin(SetMovingFalsePath, IsMovingVar))
		{
			FalseValuePin->DefaultValue = TEXT("false");
		}

		// Exec: Update -> Branch -> (Then: Set from Greater) / (Else: Set false)
		TryLink(UpdateEvent->FindPin(UEdGraphSchema_K2::PN_Then), Branch->GetExecPin());
		TryLink(Branch->GetThenPin(), SetMovingTruePath->GetExecPin());
		TryLink(Branch->GetElsePin(), SetMovingFalsePath->GetExecPin());

		UE_LOG(LogFPSAnimConnect, Display, TEXT("Rebuilt EventGraph: Update->IsValid->Set IsMoving"));
		return true;
	}

	UAnimGraphNode_TransitionResult* FindTransitionResult(UEdGraph* TransGraph)
	{
		if (!TransGraph)
		{
			return nullptr;
		}
		for (UEdGraphNode* Node : TransGraph->Nodes)
		{
			if (UAnimGraphNode_TransitionResult* Result = Cast<UAnimGraphNode_TransitionResult>(Node))
			{
				return Result;
			}
		}
		return nullptr;
	}

	bool WireIsMovingTransitionRule(UAnimStateTransitionNode* Transition, FName IsMovingVar, bool bWantMoving)
	{
		if (!Transition || !Transition->BoundGraph)
		{
			return false;
		}

		UEdGraph* Graph = Transition->BoundGraph;
		UAnimGraphNode_TransitionResult* ResultNode = FindTransitionResult(Graph);
		if (!ResultNode)
		{
			return false;
		}

		TArray<UEdGraphNode*> ToDelete;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node && Node != ResultNode)
			{
				ToDelete.Add(Node);
			}
		}
		for (UEdGraphNode* Node : ToDelete)
		{
			DeleteNodeSafe(Graph, Node);
		}

		FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Graph);
		UK2Node_VariableGet* GetIsMoving = GetCreator.CreateNode();
		GetIsMoving->VariableReference.SetSelfMember(IsMovingVar);
		GetIsMoving->NodePosX = ResultNode->NodePosX - 450;
		GetIsMoving->NodePosY = ResultNode->NodePosY;
		GetCreator.Finalize();
		GetIsMoving->AllocateDefaultPins();
		GetIsMoving->ReconstructNode();

		UEdGraphPin* BoolOut = FindDataOutputPin(GetIsMoving);
		UEdGraphPin* BoolIn = FindTransitionBoolPin(ResultNode);
		bool bOk = false;

		if (bWantMoving)
		{
			bOk = TryLink(BoolOut, BoolIn);
			UE_LOG(LogFPSAnimConnect, Display, TEXT("Transition rule IsMoving == true -> %d"), bOk ? 1 : 0);
		}
		else
		{
			UK2Node_CallFunction* NotNode = CreateCall(
				Graph,
				UKismetMathLibrary::StaticClass(),
				TEXT("Not_PreBool"),
				ResultNode->NodePosX - 220,
				ResultNode->NodePosY);
			TryLink(BoolOut, FindDataInputPin(NotNode, TEXT("A")));
			bOk = TryLink(NotNode->GetReturnValuePin(), BoolIn);
			UE_LOG(LogFPSAnimConnect, Display, TEXT("Transition rule IsMoving == false -> %d"), bOk ? 1 : 0);
		}

		return bOk;
	}

	UAnimStateTransitionNode* FindOrCreateTransition(
		UAnimationStateMachineGraph* SMGraph,
		UAnimStateNode* FromState,
		UAnimStateNode* ToState)
	{
		if (!SMGraph || !FromState || !ToState)
		{
			return nullptr;
		}

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Node))
			{
				if (Trans->GetPreviousState() == FromState && Trans->GetNextState() == ToState)
				{
					return Trans;
				}
			}
		}

		UEdGraphPin* FromOut = FromState->GetOutputPin();
		UEdGraphPin* ToIn = ToState->GetInputPin();
		if (!FromOut || !ToIn)
		{
			return nullptr;
		}

		const UEdGraphSchema* Schema = SMGraph->GetSchema();
		if (Schema)
		{
			Schema->TryCreateConnection(FromOut, ToIn);
		}

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateTransitionNode* Trans = Cast<UAnimStateTransitionNode>(Node))
			{
				if (Trans->GetPreviousState() == FromState && Trans->GetNextState() == ToState)
				{
					UE_LOG(LogFPSAnimConnect, Display, TEXT("Created transition %s -> %s"),
						*FromState->GetStateName(), *ToState->GetStateName());
					return Trans;
				}
			}
		}

		UE_LOG(LogFPSAnimConnect, Warning, TEXT("Could not create transition %s -> %s"),
			*FromState->GetStateName(), *ToState->GetStateName());
		return nullptr;
	}
}

bool UFPSAnimConnectBPLibrary::WireArmsIdleToOutputPose()
{
	using namespace FPSAnimConnectLocal;

	const FString AnimBPPath = TEXT("/Game/Blueprints/Character/Animation/FPS_AnimBP.FPS_AnimBP");
	const FString IdlePath = TEXT("/Game/Assets/Characters/Arms/Arms_Animations/Arms_idle.Arms_idle");
	const FString WalkPath = TEXT("/Game/Assets/Characters/Arms/Arms_Animations/Arms_Walk.Arms_Walk");
	const FString MeshPath = TEXT("/Game/Assets/Characters/Arms/Mesh/Mesh_Arms.Mesh_Arms");

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	UAnimSequence* IdleSequence = LoadObject<UAnimSequence>(nullptr, *IdlePath);
	UAnimSequence* WalkSequence = LoadObject<UAnimSequence>(nullptr, *WalkPath);
	USkeletalMesh* PreviewMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);

	if (!AnimBP || !IdleSequence)
	{
		UE_LOG(LogFPSAnimConnect, Error, TEXT("Failed to load FPS_AnimBP or Arms_idle"));
		return false;
	}

	AnimBP->SetPreviewAnimationBlueprint(nullptr);
	if (PreviewMesh)
	{
		AnimBP->SetPreviewMesh(PreviewMesh, true);
	}
	UE_LOG(LogFPSAnimConnect, Display, TEXT("Preview overlay cleared; preview mesh=%s"),
		PreviewMesh ? *PreviewMesh->GetName() : TEXT("null"));

	RemoveUnusedSpeedVariable(AnimBP);
	const FName IsMovingVar = EnsureIsMovingVariable(AnimBP);
	const bool bEventOk = RebuildIsMovingEventGraph(AnimBP, IsMovingVar);

	UEdGraph* AnimGraph = FindAnimGraph(AnimBP);
	if (!AnimGraph)
	{
		UE_LOG(LogFPSAnimConnect, Error, TEXT("AnimGraph not found"));
		return false;
	}

	UAnimGraphNode_Root* RootNode = nullptr;
	UAnimGraphNode_StateMachine* StateMachineNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (!RootNode) RootNode = Cast<UAnimGraphNode_Root>(Node);
		if (!StateMachineNode) StateMachineNode = Cast<UAnimGraphNode_StateMachine>(Node);
	}

	if (!RootNode)
	{
		UE_LOG(LogFPSAnimConnect, Error, TEXT("Output Pose root not found"));
		return false;
	}

	bool bIdleWired = false;
	bool bWalkWired = false;
	bool bSmToRoot = false;
	bool bTransOk = false;

	if (StateMachineNode && StateMachineNode->EditorStateMachineGraph)
	{
		UAnimationStateMachineGraph* SMGraph = StateMachineNode->EditorStateMachineGraph;
		UAnimStateNode* IdleState = nullptr;
		UAnimStateNode* WalkState = nullptr;
		UAnimStateEntryNode* EntryNode = nullptr;

		for (UEdGraphNode* Node : SMGraph->Nodes)
		{
			if (UAnimStateEntryNode* Entry = Cast<UAnimStateEntryNode>(Node))
			{
				EntryNode = Entry;
			}
			if (UAnimStateNode* State = Cast<UAnimStateNode>(Node))
			{
				const FString Name = State->GetStateName();
				UE_LOG(LogFPSAnimConnect, Display, TEXT("Found state: %s"), *Name);
				if (Name.Equals(TEXT("Idle"), ESearchCase::IgnoreCase))
				{
					IdleState = State;
				}
				else if (Name.Equals(TEXT("Walking"), ESearchCase::IgnoreCase)
					|| Name.Equals(TEXT("Walk"), ESearchCase::IgnoreCase))
				{
					WalkState = State;
				}
			}
		}

		if (IdleState)
		{
			bIdleWired = WireStateAnimation(IdleState, IdleSequence);
			UE_LOG(LogFPSAnimConnect, Display, TEXT("Idle Arms_idle -> Result: %d"), bIdleWired ? 1 : 0);
		}

		if (WalkState && WalkSequence)
		{
			bWalkWired = WireStateAnimation(WalkState, WalkSequence);
			UE_LOG(LogFPSAnimConnect, Display, TEXT("Walking Arms_Walk -> Result: %d"), bWalkWired ? 1 : 0);
		}
		else if (WalkState && !WalkSequence)
		{
			UE_LOG(LogFPSAnimConnect, Warning, TEXT("Arms_Walk failed to load"));
		}

		if (EntryNode && IdleState)
		{
			UEdGraphPin* EntryOut = EntryNode->GetOutputPin();
			UEdGraphPin* IdleIn = IdleState->GetInputPin();
			if (EntryOut && IdleIn)
			{
				EntryOut->BreakAllPinLinks(true);
				const UEdGraphSchema* Schema = SMGraph->GetSchema();
				if (!(Schema && Schema->TryCreateConnection(EntryOut, IdleIn)))
				{
					EntryOut->MakeLinkTo(IdleIn);
				}
				UE_LOG(LogFPSAnimConnect, Display, TEXT("Entry -> Idle links=%d"), IdleIn->LinkedTo.Num());
			}
		}

		if (IdleState && WalkState)
		{
			UAnimStateTransitionNode* IdleToWalk = FindOrCreateTransition(SMGraph, IdleState, WalkState);
			UAnimStateTransitionNode* WalkToIdle = FindOrCreateTransition(SMGraph, WalkState, IdleState);

			bool bA = false;
			bool bB = false;
			if (IdleToWalk)
			{
				bA = WireIsMovingTransitionRule(IdleToWalk, IsMovingVar, true);
			}
			if (WalkToIdle)
			{
				bB = WireIsMovingTransitionRule(WalkToIdle, IsMovingVar, false);
			}
			bTransOk = bA && bB;
			UE_LOG(LogFPSAnimConnect, Display, TEXT("Transitions Idle<->Walk via IsMoving: %d"), bTransOk ? 1 : 0);
		}

		bSmToRoot = TryLink(FindPoseOutputPin(StateMachineNode), FindResultInputPin(RootNode));
		UE_LOG(LogFPSAnimConnect, Display, TEXT("StateMachine -> Output Pose: %d"), bSmToRoot ? 1 : 0);
	}

	if (!bSmToRoot)
	{
		UAnimGraphNode_SequencePlayer* TopPlayer = EnsureSequencePlayer(
			AnimGraph,
			IdleSequence,
			RootNode->NodePosX - 400,
			RootNode->NodePosY);
		bIdleWired = TryLink(FindPoseOutputPin(TopPlayer), FindResultInputPin(RootNode));
		UE_LOG(LogFPSAnimConnect, Display, TEXT("Fallback top-level Arms_idle -> Output Pose: %d"), bIdleWired ? 1 : 0);
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	const bool bSaved = SaveAssetPackage(AnimBP);

	const bool bOk = bSaved && bEventOk && (bIdleWired || bSmToRoot);
	UE_LOG(LogFPSAnimConnect, Display,
		TEXT("Fix complete saved=%d event=%d idle=%d walk=%d trans=%d sm=%d ok=%d"),
		bSaved ? 1 : 0, bEventOk ? 1 : 0, bIdleWired ? 1 : 0, bWalkWired ? 1 : 0,
		bTransOk ? 1 : 0, bSmToRoot ? 1 : 0, bOk ? 1 : 0);
	return bOk;
}

bool UFPSAnimConnectBPLibrary::FixCharacterAimIsAiming()
{
	using namespace FPSAnimConnectLocal;

	const FString CharPath = TEXT("/Game/Blueprints/Character/FPS_Character.FPS_Character");
	const FString AnimBPClassPath = TEXT("/Game/Blueprints/Character/Animation/FPS_AnimBP.FPS_AnimBP_C");
	static const FName AnimBPVarName(TEXT("AnimationBlueprint"));
	static const FName IsAimingName(TEXT("IsAiming"));
	static const FName AimActionName(TEXT("Aim"));

	UBlueprint* CharBP = LoadObject<UBlueprint>(nullptr, *CharPath);
	LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Blueprints/Character/Animation/FPS_AnimBP.FPS_AnimBP"));
	UClass* AnimBPClass = LoadClass<UAnimInstance>(nullptr, *AnimBPClassPath);
	if (!CharBP || !AnimBPClass)
	{
		UE_LOG(LogFPSAnimConnect, Error, TEXT("Failed to load FPS_Character or FPS_AnimBP_C"));
		return false;
	}
	if (CharBP->UbergraphPages.Num() == 0)
	{
		return false;
	}
	UEdGraph* Graph = CharBP->UbergraphPages[0];

	auto DeleteNode = [&](UEdGraphNode* Node)
	{
		if (!Node) return;
		Node->BreakAllNodeLinks();
		Graph->RemoveNode(Node);
	};

	auto MakeVarGet = [&](FName VarName, int32 X, int32 Y) -> UK2Node_VariableGet*
	{
		FGraphNodeCreator<UK2Node_VariableGet> Creator(*Graph);
		UK2Node_VariableGet* Node = Creator.CreateNode();
		Node->VariableReference.SetSelfMember(VarName);
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		Node->AllocateDefaultPins();
		Node->ReconstructNode();
		return Node;
	};

	auto MakeVarSet = [&](FName VarName, UClass* OwnerClass, int32 X, int32 Y) -> UK2Node_VariableSet*
	{
		FGraphNodeCreator<UK2Node_VariableSet> Creator(*Graph);
		UK2Node_VariableSet* Node = Creator.CreateNode();
		if (OwnerClass)
		{
			Node->VariableReference.SetExternalMember(VarName, OwnerClass);
		}
		else
		{
			Node->VariableReference.SetSelfMember(VarName);
		}
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		Node->AllocateDefaultPins();
		Node->ReconstructNode();
		return Node;
	};

	auto FindDataOut = [](UEdGraphNode* Node) -> UEdGraphPin*
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				return Pin;
			}
		}
		return nullptr;
	};

	auto FindDataIn = [](UEdGraphNode* Node, FName Preferred = NAME_None) -> UEdGraphPin*
	{
		if (Preferred != NAME_None)
		{
			if (UEdGraphPin* P = Node->FindPin(Preferred, EGPD_Input)) return P;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin && Pin->Direction == EGPD_Input
				&& Pin->PinName != UEdGraphSchema_K2::PN_Execute
				&& Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec
				&& Pin->PinName != UEdGraphSchema_K2::PN_Self)
			{
				return Pin;
			}
		}
		return nullptr;
	};

	bool bHasVar = false;
	for (const FBPVariableDescription& Var : CharBP->NewVariables)
	{
		if (Var.VarName == AnimBPVarName)
		{
			bHasVar = true;
			break;
		}
	}
	if (!bHasVar)
	{
		FEdGraphPinType ObjType;
		ObjType.PinCategory = UEdGraphSchema_K2::PC_Object;
		ObjType.PinSubCategoryObject = AnimBPClass;
		FBlueprintEditorUtils::AddMemberVariable(CharBP, AnimBPVarName, ObjType);
		UE_LOG(LogFPSAnimConnect, Display, TEXT("Created AnimationBlueprint variable"));
	}

	UK2Node_Event* BeginPlay = nullptr;
	UK2Node_InputActionEvent* AimPressed = nullptr;
	UK2Node_InputActionEvent* AimReleased = nullptr;
	TArray<UEdGraphNode*> StaleAimNodes;

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			if (EventNode->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay")
				|| EventNode->GetFunctionName() == TEXT("ReceiveBeginPlay"))
			{
				BeginPlay = EventNode;
			}
		}
		if (UK2Node_InputActionEvent* ActionEvt = Cast<UK2Node_InputActionEvent>(Node))
		{
			const bool bIsAim = (ActionEvt->InputActionName == AimActionName)
				|| ActionEvt->CustomFunctionName.ToString().Contains(TEXT("Aim"));
			if (bIsAim)
			{
				if (ActionEvt->InputKeyEvent == IE_Pressed) AimPressed = ActionEvt;
				else if (ActionEvt->InputKeyEvent == IE_Released) AimReleased = ActionEvt;
			}
		}
		if (UK2Node_VariableSet* SetNode = Cast<UK2Node_VariableSet>(Node))
		{
			if (SetNode->GetVarName() == IsAimingName || SetNode->GetVarName() == AnimBPVarName)
			{
				StaleAimNodes.Add(SetNode);
			}
		}
		if (UK2Node_VariableGet* GetNode = Cast<UK2Node_VariableGet>(Node))
		{
			if (GetNode->GetVarName() == AnimBPVarName)
			{
				StaleAimNodes.Add(GetNode);
			}
		}
	}

	for (UEdGraphNode* Node : StaleAimNodes)
	{
		DeleteNode(Node);
	}

	if (!BeginPlay)
	{
		FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
		BeginPlay = Creator.CreateNode();
		BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
		BeginPlay->bOverrideFunction = true;
		BeginPlay->NodePosX = 0;
		BeginPlay->NodePosY = 0;
		Creator.Finalize();
		BeginPlay->AllocateDefaultPins();
		BeginPlay->ReconstructNode();
	}

	if (!AimPressed)
	{
		FGraphNodeCreator<UK2Node_InputActionEvent> Creator(*Graph);
		AimPressed = Creator.CreateNode();
		AimPressed->CustomFunctionName = FName(TEXT("InpActEvt_Aim_Pressed"));
		AimPressed->InputActionName = AimActionName;
		AimPressed->InputKeyEvent = IE_Pressed;
		AimPressed->bConsumeInput = true;
		AimPressed->NodePosX = 0;
		AimPressed->NodePosY = 400;
		Creator.Finalize();
		AimPressed->AllocateDefaultPins();
		AimPressed->ReconstructNode();
	}

	if (!AimReleased)
	{
		FGraphNodeCreator<UK2Node_InputActionEvent> Creator(*Graph);
		AimReleased = Creator.CreateNode();
		AimReleased->CustomFunctionName = FName(TEXT("InpActEvt_Aim_Released"));
		AimReleased->InputActionName = AimActionName;
		AimReleased->InputKeyEvent = IE_Released;
		AimReleased->bConsumeInput = true;
		AimReleased->NodePosX = 0;
		AimReleased->NodePosY = 700;
		Creator.Finalize();
		AimReleased->AllocateDefaultPins();
		AimReleased->ReconstructNode();
	}

	UK2Node_VariableGet* MeshGet = MakeVarGet(TEXT("Mesh"), 250, 0);
	UK2Node_CallFunction* GetAnim = CreateCall(
		Graph, USkeletalMeshComponent::StaticClass(), TEXT("GetAnimInstance"), 500, 0);

	FGraphNodeCreator<UK2Node_DynamicCast> CastCreator(*Graph);
	UK2Node_DynamicCast* CastNode = CastCreator.CreateNode();
	CastNode->TargetType = AnimBPClass;
	CastNode->NodePosX = 750;
	CastNode->NodePosY = 0;
	CastCreator.Finalize();
	CastNode->AllocateDefaultPins();
	CastNode->ReconstructNode();

	UK2Node_VariableSet* SetAnimBP = MakeVarSet(AnimBPVarName, nullptr, 1100, 0);

	TryLink(FindDataOut(MeshGet), GetAnim->FindPin(UEdGraphSchema_K2::PN_Self));
	UEdGraphPin* CastObj = CastNode->FindPin(UEdGraphSchema_K2::PN_ObjectToCast);
	if (!CastObj) CastObj = CastNode->FindPin(TEXT("Object"), EGPD_Input);
	TryLink(GetAnim->GetReturnValuePin(), CastObj);
	TryLink(CastNode->GetCastResultPin(), FindDataIn(SetAnimBP, AnimBPVarName));

	if (UEdGraphPin* BeginThen = BeginPlay->FindPin(UEdGraphSchema_K2::PN_Then))
	{
		UEdGraphPin* Prev = (BeginThen->LinkedTo.Num() > 0) ? BeginThen->LinkedTo[0] : nullptr;
		BeginThen->BreakAllPinLinks(true);
		TryLink(BeginThen, CastNode->GetExecPin());
		TryLink(CastNode->GetThenPin(), SetAnimBP->GetExecPin());
		if (Prev && Prev->GetOwningNode() != CastNode && Prev->GetOwningNode() != SetAnimBP)
		{
			TryLink(SetAnimBP->FindPin(UEdGraphSchema_K2::PN_Then), Prev);
		}
	}
	UE_LOG(LogFPSAnimConnect, Display, TEXT("BeginPlay sets AnimationBlueprint from Mesh AnimInstance"));

	auto WireAim = [&](UK2Node_InputActionEvent* Evt, bool bValue, int32 Y)
	{
		UK2Node_VariableGet* GetAnimBP = MakeVarGet(AnimBPVarName, 250, Y);
		UK2Node_CallFunction* IsValidNode = CreateCall(
			Graph, UKismetSystemLibrary::StaticClass(), TEXT("IsValid"), 500, Y);

		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
		UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
		Branch->NodePosX = 750;
		Branch->NodePosY = Y;
		BranchCreator.Finalize();

		UK2Node_VariableSet* SetAiming = MakeVarSet(IsAimingName, AnimBPClass, 1050, Y);

		TryLink(FindDataOut(GetAnimBP), IsValidNode->FindPinChecked(TEXT("Object")));
		TryLink(IsValidNode->GetReturnValuePin(), Branch->GetConditionPin());
		TryLink(FindDataOut(GetAnimBP), SetAiming->FindPin(UEdGraphSchema_K2::PN_Self));
		if (UEdGraphPin* ValuePin = FindDataIn(SetAiming, IsAimingName))
		{
			ValuePin->DefaultValue = bValue ? TEXT("true") : TEXT("false");
		}

		if (UEdGraphPin* ThenPin = Evt->FindPin(UEdGraphSchema_K2::PN_Then))
		{
			ThenPin->BreakAllPinLinks(true);
			TryLink(ThenPin, Branch->GetExecPin());
		}
		TryLink(Branch->GetThenPin(), SetAiming->GetExecPin());
	};

	WireAim(AimPressed, true, 400);
	WireAim(AimReleased, false, 700);
	UE_LOG(LogFPSAnimConnect, Display, TEXT("Aim Pressed/Released -> IsAiming via AnimationBlueprint + IsValid"));

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(CharBP);
	FKismetEditorUtilities::CompileBlueprint(CharBP);
	const bool bSaved = SaveAssetPackage(CharBP);
	UE_LOG(LogFPSAnimConnect, Display, TEXT("FixCharacterAimIsAiming saved=%d"), bSaved ? 1 : 0);
	return bSaved;
}

bool UFPSAnimConnectBPLibrary::FixAimWalkLoop()
{
	using namespace FPSAnimConnectLocal;

	const FString AnimBPPath = TEXT("/Game/Blueprints/Character/Animation/FPS_AnimBP.FPS_AnimBP");
	const FString AimWalkPath = TEXT("/Game/Assets/Characters/Arms/Arms_Animations/Arms_Aim_Walk.Arms_Aim_Walk");

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *AnimBPPath);
	UAnimSequence* AimWalk = LoadObject<UAnimSequence>(nullptr, *AimWalkPath);
	if (!AnimBP || !AimWalk)
	{
		UE_LOG(LogFPSAnimConnect, Error, TEXT("Failed to load FPS_AnimBP or Arms_Aim_Walk"));
		return false;
	}

	// Ensure asset itself is marked looping (preview / default)
	AimWalk->bLoop = true;
	AimWalk->MarkPackageDirty();
	const bool bSeqSaved = SaveAssetPackage(AimWalk);
	UE_LOG(LogFPSAnimConnect, Display, TEXT("Arms_Aim_Walk bLoop=1 saved=%d"), bSeqSaved ? 1 : 0);

	int32 PlayersFixed = 0;
	int32 StatesFixed = 0;

	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (!Graph)
		{
			continue;
		}

		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UAnimStateNode* StateNode = Cast<UAnimStateNode>(Node))
			{
				const FString Name = StateNode->GetStateName();
				if (Name.Equals(TEXT("Aim Walking"), ESearchCase::IgnoreCase)
					|| Name.Equals(TEXT("AimWalk"), ESearchCase::IgnoreCase)
					|| Name.Equals(TEXT("Aiming Walk"), ESearchCase::IgnoreCase))
				{
					if (WireStateAnimation(StateNode, AimWalk))
					{
						++StatesFixed;
						UE_LOG(LogFPSAnimConnect, Display, TEXT("Wired+looped state: %s"), *Name);
					}
				}
			}

			if (UAnimGraphNode_SequencePlayer* Player = Cast<UAnimGraphNode_SequencePlayer>(Node))
			{
				UAnimationAsset* Asset = Player->GetAnimationAsset();
				const bool bIsAimWalk = (Asset == AimWalk)
					|| (Asset && Asset->GetName().Contains(TEXT("Arms_Aim_Walk")));
				if (bIsAimWalk)
				{
					Player->SetAnimationAsset(AimWalk);
					Player->Node.SetSequence(AimWalk);
					Player->Node.SetLoopAnimation(true);
					Player->ReconstructNode();
					++PlayersFixed;
					UE_LOG(LogFPSAnimConnect, Display, TEXT("Forced loop on SequencePlayer using Arms_Aim_Walk in %s"),
						*Graph->GetName());
				}
			}
		}
	}

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);
	const bool bBPSaved = SaveAssetPackage(AnimBP);

	const bool bOk = bBPSaved && (PlayersFixed > 0 || StatesFixed > 0);
	UE_LOG(LogFPSAnimConnect, Display,
		TEXT("FixAimWalkLoop saved=%d players=%d states=%d ok=%d"),
		bBPSaved ? 1 : 0, PlayersFixed, StatesFixed, bOk ? 1 : 0);
	return bOk;
}

