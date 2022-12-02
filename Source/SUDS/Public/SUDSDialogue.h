﻿#pragma once

#include "CoreMinimal.h"
#include "SUDSScriptNode.h"
#include "SUDSExpression.h"
#include "UObject/Object.h"
#include "SUDSDialogue.generated.h"

class USUDSScriptNodeText;
struct FSUDSScriptEdge;
class USUDSScriptNode;
class USUDSScript;


DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueSpeakerLine, class USUDSDialogue*, Dialogue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueChoice, class USUDSDialogue*, Dialogue, int, ChoiceIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueProceeding, class USUDSDialogue*, Dialogue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDialogueStarting, class USUDSDialogue*, Dialogue, FName, AtLabel);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDialogueFinished, class USUDSDialogue*, Dialogue);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnDialogueEvent, class USUDSDialogue*, Dialogue, FName, EventName, const TArray<FSUDSValue>&, Arguments);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnVariableChangedEvent, class USUDSDialogue*, Dialogue, FName, VariableName, const FSUDSValue&, Value, bool, bFromScript);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnVariableRequestedEvent, class USUDSDialogue*, Dialogue, FName, VariableName);

DECLARE_LOG_CATEGORY_EXTERN(LogSUDSDialogue, Verbose, All);

/// Copy of the internal state of a dialogue
USTRUCT(BlueprintType)
struct FSUDSDialogueState
{
	GENERATED_BODY()
protected:
	UPROPERTY(BlueprintReadOnly)
	FString TextNodeID;

	UPROPERTY(BlueprintReadOnly)
	TMap<FName, FSUDSValue> Variables;

	UPROPERTY(BlueprintReadOnly)
	TArray<FString> ChoicesTaken;
	
public:
	FSUDSDialogueState() {}

	FSUDSDialogueState(const FString& TxtID,
	                   const TMap<FName, FSUDSValue>& InVars,
	                   const TSet<FString>& InChoices) : TextNodeID(TxtID),
	                                                     Variables(InVars),
	                                                     ChoicesTaken(InChoices.Array())
	{
	}

	const FString& GetTextNodeID() const { return TextNodeID; }
	const TMap<FName, FSUDSValue>& GetVariables() const { return Variables; }
	const TArray<FString>& GetChoicesTaken() const { return ChoicesTaken; }

	SUDS_API friend FArchive& operator<<(FArchive& Ar, FSUDSDialogueState& Value);
	SUDS_API friend void operator<<(FStructuredArchive::FSlot Slot, FSUDSDialogueState& Value);
	bool Serialize(FStructuredArchive::FSlot Slot)
	{
		Slot << *this;
		return true;
	}
	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
	
};
/**
 * A Dialogue is a runtime instance of a Script (the asset on which the dialogue is based)
 * An Dialogue always stops on a speaker line, which may have player choices. It progresses when you call Continue()
 * or Choose() and will run that continuation until it hits the next speaker line. In between, other things may occur
 * such as setting variables, raising events etc, depending on the script. 
 * Each dialogue instance has its own state, so you can invoke the same Script multiple times as different dialogues if you want.
 * Each dialogue maintains its own internal state, which includes a set of variables. 
 * Dialogues can have Participants, which are objects closely involved in the dialogue and which have the best access to
 * supply and retrieve variables and get events first. Other objects can simply listen to the exposed events; while they
 * can manipulate dialogue state too, they have less controllable access in terms of *when* this happens. It's best to
 * have at least one Participant driving state on the dialogue (relaying it to external objects), and to have read-only
 * users like UIs use the event delegates instead.
 * Dialogues need to be owned by an object, mainly for garbage collection. It's recommended that you set the owner to
 * one of the NPCs in the dialogue.
 * You can save/restore the state of a dialogue via GetSavedState/RestoreSavedState. 
 */
UCLASS(BlueprintType)
class SUDS_API USUDSDialogue : public UObject
{
	GENERATED_BODY()
public:
	/// Event raised when dialogue progresses and a new speaker line, potentially with new choices, is ready to be displayed
	UPROPERTY(BlueprintAssignable)
	FOnDialogueSpeakerLine OnSpeakerLine;
	/// Event raised when a choice is made in the dialogue by the player. At this point, the dialogue has not progressed
	/// as a result of that choice so the index passed can be used to reference the choice
	/// This event is ONLY raised if there's a choice of paths, not for just continuing a linear path.
	UPROPERTY(BlueprintAssignable)
	FOnDialogueChoice OnChoice;
	/// Event raised when the dialog is about to proceed away from the current speaker line (because of a choice or continue)
	UPROPERTY(BlueprintAssignable)
	FOnDialogueProceeding OnProceeding;
	/// Event raised when an event is sent from the dialogue script. Any listeners or participants can process the event.
	UPROPERTY(BlueprintAssignable)
	FOnDialogueEvent OnEvent;
	/// Event raised when a variable is changed. "FromScript" is true if the variable was set by the script, false if set from code
	UPROPERTY(BlueprintAssignable)
	FOnVariableChangedEvent OnVariableChanged;
	/// Event raised when a variable is requested by the dialogue script. You can use this hook to set variables in the
	/// dialogue on-demand rather than up-front; anything set during this hook will be immediately used by the dialogue 
	UPROPERTY(BlueprintAssignable)
	FOnVariableRequestedEvent OnVariableRequested;
	/// Event raised when the dialogue is starting, before the first speaker line
	UPROPERTY(BlueprintAssignable)
	FOnDialogueStarting OnStarting;
	/// Event raised when the dialogue finishes
	UPROPERTY(BlueprintAssignable)
	FOnDialogueFinished OnFinished;
protected:
	UPROPERTY()
	const USUDSScript* BaseScript;
	UPROPERTY()
	USUDSScriptNodeText* CurrentSpeakerNode;
	/// External objects which want to closely participate in the dialogue (not just listen to events)
	UPROPERTY()
	TArray<UObject*> Participants;
	

	/// All of the dialogue variables
	/// Dialogue variable state is all held locally. Dialogue participants can retrieve or set values in state.
	/// All state is saved with the dialogue. Variables can be used as text substitution parameters, conditionals,
	/// or communication with external state.
	typedef TMap<FName, FSUDSValue> FSUDSValueMap;
	FSUDSValueMap VariableState;

	/// Set of all the TextIDs of choices taken already in this dialogue
	TSet<FString> ChoicesTaken;

	TSet<FName> CurrentRequestedParamNames;
	bool bParamNamesExtracted;
	
	/// Cached derived info
	mutable FText CurrentSpeakerDisplayName;
	/// All valid choices
	TArray<FSUDSScriptEdge> CurrentChoices;
	static const FText DummyText;
	static const FString DummyString;

	void RunUntilNextSpeakerNodeOrEnd(USUDSScriptNode* FromNode, bool bRaiseAtEnd);
	const USUDSScriptNode* RunUntilNextChoiceNode(const USUDSScriptNodeText* FromTextNode);
	void SetCurrentSpeakerNode(USUDSScriptNodeText* Node, bool bQuietly);
	void SortParticipants();
	void RaiseStarting(FName StartLabel);
	void RaiseFinished();
	void RaiseNewSpeakerLine();
	void RaiseChoiceMade(int Index);
	void RaiseProceeding();
	void RaiseVariableChange(const FName& VarName, const FSUDSValue& Value, bool bFromScript);
	void RaiseVariableRequested(const FName& VarName);
	void RaiseExpressionVariablesRequested(const FSUDSExpression& Expression);

	USUDSScriptNode* GetNextNode(const USUDSScriptNode* Node) const;
	bool ShouldStopAtNodeType(ESUDSScriptNodeType Type);
	USUDSScriptNode* RunNode(USUDSScriptNode* Node);
	USUDSScriptNode* RunSelectNode(USUDSScriptNode* Node);
	USUDSScriptNode* RunSetVariableNode(USUDSScriptNode* Node);
	USUDSScriptNode* RunEventNode(USUDSScriptNode* Node);
	void UpdateChoices();
	void RecurseAppendChoices(const USUDSScriptNode* Node, TArray<FSUDSScriptEdge>& OutChoices);

	void GetTextFormatArgs(const TArray<FName>& ArgNames, FFormatNamedArguments& OutArgs) const;
public:
	USUDSDialogue();
	void Initialise(const USUDSScript* Script);
	void InitVariables();

	/**
	 * Begin the dialogue. Make sure you've added all participants before calling this.
	 * This may not be the first time you've started this dialogue. All previous state is maintained to enable you
	 * for example to take branching paths based on whether you've spoken to this character before.
	 * If you want to reset *all* state, call Restart(true). However this is an extreme case; if you want to just
	 * reset some variables then use the header section of the script to set variables to a default starting point.
	 * @param Label The start point for this dialogue. If None, starts from the beginning.
	 */
	UFUNCTION(BlueprintCallable)
	void Start(FName Label = NAME_None);


	/**
	 * Add a participant to this dialogue instance.
	 * Participants are objects which want to be more closely involved in the dialogue. As opposed to event listeners,
	 * participants get advance notice of events in the dialogue, and are also called in a known order, determined by
	 * their priority. If you're providing variables to the dialogue, it is best to do it as a participant since it
	 * gives you much more control.
	 * @param Participant The participant object, which must implement ISUDSParticipant
	 */
	UFUNCTION(BlueprintCallable)
	void AddParticipant(UObject* Participant);

	/// Retrieve participants from this dialogue
	UFUNCTION(BlueprintCallable)
	const TArray<UObject*>& GetParticipants() const { return Participants; }
	
	/**
	 * Set the complete list of participants for this dialogue instance.
	 * Participants are objects which want to be more closely involved in the dialogue. As opposed to event listeners,
	 * participants get advance notice of events in the dialogue, and are also called in a known order, determined by
	 * their priority. If you're providing variables to the dialogue, it is best to do it as a participant since it
	 * gives you much more control.
	 * @param NewParticipants List of new participants. Each should implement ISUDSParticipant
	 */
	UFUNCTION(BlueprintCallable)
	void SetParticipants(const TArray<UObject*>& NewParticipants);
	

	/// Get the speech text for the current dialogue node
	/// Any parameters required will be requested from participants in the dialogue and replaced 
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FText GetText() const;

	/// Get the ID of the current speaker
	UFUNCTION(BlueprintCallable, BlueprintPure)
	const FString& GetSpeakerID() const;

	/// Get the display name of the current speaker
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FText GetSpeakerDisplayName() const;

	/**
	 * Get the number of choices available from this node.
	 * Note, this will return 1 in the case of just linear text progression. The difference between just linked text
	 * lines and a choice with only 1 option is whether the choice text is blank or not.
	 * @return The number of choices available
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	int GetNumberOfChoices() const;

	/**
	 * Get the text associated with a choice.
	 * @param Index The index of the choice
	 * @return The text. This may be blank if this represents just a link between 2 nodes and not a choice at all.
	 *    Note that if you want to have only 1 choice but with associated text, this is fine and should be a choice
	 *    line just like any other.
	 */
	UFUNCTION(BlueprintCallable)
	FText GetChoiceText(int Index) const;

	/// Get all the current choices available, if you prefer this format
	UFUNCTION(BlueprintCallable)
	const TArray<FSUDSScriptEdge>& GetChoices() const;

	/** Returns whether the choice at the given index has been taken previously.
	*	This is saved in dialogue state so will be remembered across save/restore.
	*/
	UFUNCTION(BlueprintCallable)
	bool HasChoiceIndexBeenTakenPreviously(int Index);

	/** Returns whether a choice has been taken previously.
	*	This is saved in dialogue state so will be remembered across save/restore.
	*/
	UFUNCTION(BlueprintCallable)
	bool HasChoiceBeenTakenPreviously(const FSUDSScriptEdge& Choice);
	
	
	/**
	 * Continues the dialogue if (and ONLY if) there is only one valid path/choice out of the current node.
	 * @return True if the dialogue continues after this, false if the dialogue is now at an end.
	 */
	UFUNCTION(BlueprintCallable)
	bool Continue();

	/**
	 * Picks one of the available choices 
	 * If there's only 1 you can still call this with Index = 0, but also see Continue
	 * @param Index The index of the choice to make
	 * @return True if the dialogue continues, false if it has now reached the end.
	 */
	UFUNCTION(BlueprintCallable)
	bool Choose(int Index);

	/// Returns true if the dialogue has reached the end
	UFUNCTION(BlueprintCallable, BlueprintPure)
	bool IsEnded() const;

	/// End the dialogue early
	UFUNCTION(BlueprintCallable)
	void End(bool bQuietly);
	
	/**
	 * Restart the dialogue, either from the start or from a named label.
	 * @param bResetState Whether to reset ALL dialogue state, as if the dialogue had been created anew. You mostly don't want
	 * to do this; if you have certain things you want to reset every time, then use [set] commands in the header section
	 * which runs every time the dialogue starts.
	 * @param StartLabel Label to start running from; if None start from the beginning.
	 * @param bReRunHeader If true (default), re-runs the header nodes before starting. Header nodes let you initialise
	 *   state that should always be reset when the dialogue is restarted
	 */
	UFUNCTION(BlueprintCallable)
	void Restart(bool bResetState = false, FName StartLabel = NAME_None, bool bReRunHeader = true);

	/**
	 * Reset the state of this dialogue.
	 * @param bResetVariables If true, resets all variable state
	 * @param bResetPosition If true, resets the current position in the dialogue (which speaker line is next)
	 * @param bResetVisited If true, resets the memory of which choices have been made
	 */
	UFUNCTION(BlueprintCallable)
	void ResetState(bool bResetVariables = true, bool bResetPosition = true, bool bResetVisited = true);

	/** Retrieve a copy of the state of this dialogue.
	 *  This is useful for saving the state of this dialogue.
	 *  @return A static copy of the current state of this dialogue. This struct can be serialised with your save data,
	 *  and contains both the state of variables and the current speaking node ID.
	 *  @note If you save/load mid-dialogue then you're need to have written Text ID's into the source text to ensure they
	 *  stay the same between edits, as you do for localisation. If you only save/load after dialogue has ended then
	 *  you don't need to worry about this since the dialogue will always start from the beginning
	 */
	UFUNCTION(BlueprintCallable)
	FSUDSDialogueState GetSavedState() const;

	/** Restore the saved state of this dialogue.
	 *  This is useful for restoring the state of this dialogue. It will attempt to restore both the value of variables,
	 *  and the current speaking node in the dialogue. If you expect to be able to restore to a point mid-dialogue,
	 *  it's important that Text IDs are defined in your source file (as for localisation) since that's used as the
	 *  identifier of the current speaking node. If you only save/load after dialogue has ended then you don't need
	 *  to worry about this as dialogue will restart each time.
	 *  @param State Dialogue state that you previously retrieved from GetSavedState().
	 *  @note After restoring, you'll want to either call Start() or Continue(), depending on whether you restored
	 *  mid-dialogue or not (see IsEnded() to tell whether you did)
	 */
	UFUNCTION(BlueprintCallable)
	void RestoreSavedState(const FSUDSDialogueState& State);
	
	/// Get the set of text parameters that are actually being asked for in the current state of the dialogue.
	/// This will include parameters in the text, and parameters in any current choices being displayed.
	/// Use this if you want to be more specific about what parameters you supply when ISUDSParticipant::UpdateDialogueParameters
	/// is called.
	UFUNCTION(BlueprintCallable)
	TSet<FName> GetParametersInUse();


	/// Set a variable in dialogue state
	/// This is mostly only useful if you happen to already have a general purpose FSUDSValue.
	/// See SetDialogueText, SetDialogueInt etc for literal-friendly versions
	UFUNCTION(BlueprintCallable)
	void SetVariable(FName Name, FSUDSValue Value)
	{
		const FSUDSValue OldValue = GetVariable(Name);
		if ((OldValue != Value).GetBooleanValue())
		{
			VariableState.Add(Name, Value);
			RaiseVariableChange(Name, Value, false);
		}
	}

	/// Get a variable in dialogue state as a general value type
	/// See GetDialogueText, GetDialogueInt etc for more type friendly versions, but if you want to access the state
	/// as a type-flexible value then you can do so with this function.
	UFUNCTION(BlueprintCallable)
	FSUDSValue GetVariable(FName Name)
	{
		if (const auto Arg = VariableState.Find(Name))
		{
			return *Arg;
		}
		return FSUDSValue();
	}

	/// Get all variables
	UFUNCTION(BlueprintCallable)
	const TMap<FName, FSUDSValue>& GetVariables() { return VariableState; }
	
	/**
	 * Set a text dialogue variable
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableText(FName Name, FText Value)
	{
		SetVariable(Name, Value);
	}

	/**
	 * Get a text dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	FText GetVariableText(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			if (Arg->GetType() == ESUDSValueType::Text)
			{
				return Arg->GetTextValue();
			}
			else
			{
				UE_LOG(LogSUDSDialogue, Error, TEXT("Requested variable %s of type text but was not a compatible type"), *Name.ToString());
			}
		}
		return FText();
	}

	/**
	 * Set a dialogue variable on the passed in parameters collection.
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableInt(FName Name, int32 Value)
	{
		SetVariable(Name, Value);
	}

	/**
	 * Get an int dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	int GetVariableInt(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			switch (Arg->GetType())
			{
			case ESUDSValueType::Int:
				return Arg->GetIntValue();
			case ESUDSValueType::Float:
				UE_LOG(LogSUDSDialogue, Warning, TEXT("Casting variable %s to int, data loss may occur"), *Name.ToString());
				return Arg->GetFloatValue();
			default: 
			case ESUDSValueType::Gender:
			case ESUDSValueType::Text:
				UE_LOG(LogSUDSDialogue, Error, TEXT("Variable %s is not a compatible integer type"), *Name.ToString());
			}
		}
		return 0;
	}
	
	/**
	 * Set a float dialogue variable 
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableFloat(FName Name, float Value)
	{
		SetVariable(Name, Value);
	}

	/**
	 * Get a float dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	float GetVariableFloat(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			switch (Arg->GetType())
			{
			case ESUDSValueType::Int:
				return Arg->GetIntValue();
			case ESUDSValueType::Float:
				return Arg->GetFloatValue();
			default: 
			case ESUDSValueType::Gender:
			case ESUDSValueType::Text:
				UE_LOG(LogSUDSDialogue, Error, TEXT("Variable %s is not a compatible float type"), *Name.ToString());
			}
		}
		return 0;
	}
	
	/**
	 * Set a gender dialogue variable 
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableGender(FName Name, ETextGender Value)
	{
		SetVariable(Name, Value);
	}

	/**
	 * Get a gender dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	ETextGender GetVariableGender(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			switch (Arg->GetType())
			{
			case ESUDSValueType::Gender:
				return Arg->GetGenderValue();
			default: 
			case ESUDSValueType::Int:
			case ESUDSValueType::Float:
			case ESUDSValueType::Text:
				UE_LOG(LogSUDSDialogue, Error, TEXT("Variable %s is not a compatible gender type"), *Name.ToString());
			}
		}
		return ETextGender::Neuter;
	}

	/**
	 * Set a boolean dialogue variable 
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableBoolean(FName Name, bool Value)
	{
		// Use explicit FSUDSValue constructor to avoid default int conversion
		SetVariable(Name, FSUDSValue(Value));
	}

	/**
	 * Get a boolean dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	bool GetVariableBoolean(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			switch (Arg->GetType())
			{
			case ESUDSValueType::Boolean:
				return Arg->GetBooleanValue();
			case ESUDSValueType::Int:
				return Arg->GetIntValue() != 0;
			default: 
			case ESUDSValueType::Float:
			case ESUDSValueType::Gender:
			case ESUDSValueType::Text:
				UE_LOG(LogSUDSDialogue, Error, TEXT("Variable %s is not a compatible boolean type"), *Name.ToString());
			}
		}
		return false;
	}
	/**
	 * Set a name dialogue variable
	 * @param Name The name of the variable
	 * @param Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	void SetVariableName(FName Name, FName Value)
	{
		SetVariable(Name, FSUDSValue(Value, false));
	}

	/**
	 * Get a name dialogue variable
	 * @param Name The name of the variable
	 * @returns Value The value of the variable
	 */
	UFUNCTION(BlueprintCallable)
	FName GetVariableName(FName Name)
	{
		if (auto Arg = VariableState.Find(Name))
		{
			if (Arg->GetType() == ESUDSValueType::Name)
			{
				return Arg->GetNameValue();
			}
			else
			{
				UE_LOG(LogSUDSDialogue, Error, TEXT("Requested variable %s of type text but was not a compatible type"), *Name.ToString());
			}
		}
		return NAME_None;
	}

};
