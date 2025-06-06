/* Copyright (c) 2024 Julian Benda
 *
 * This file is part of inkCPP which is released under MIT license.
 * See file LICENSE.txt or go to
 * https://github.com/JBenda/inkcpp for full license details.
 */
#include "InkThread.h"

#include "inkcpp.h"
#include "InkRuntime.h"
#include "TagList.h"
#include "InkChoice.h"
#include "ink/runner.h"

// Unreal includes
#include "Internationalization/Regex.h"

UInkThread::UInkThread()
    : mbHasRun(false)
    , mnChoiceToChoose(-1)
    , mnYieldCounter(0)
    , mbKill(false)
    , mCurrentKnot(~0)
{
}

UInkThread::~UInkThread() {}

void UInkThread::Yield() { mnYieldCounter++; }

bool UInkThread::IsYielding() { return mnYieldCounter > 0; }

const UTagList* UInkThread::GetKnotTags()
{
	TArray<FString> tags;
	for (size_t i = 0; i < mpRunner->num_knot_tags(); ++i) {
		tags.Add(FString(mpRunner->get_knot_tag(i)));
	}
	mkTags->Initialize(tags);
	return mkTags;
}

const UTagList* UInkThread::GetGlobalTags()
{
	if (mgTags->GetTags().Num() != mpRunner->num_global_tags()) {
		TArray<FString> tags;
		for (size_t i = 0; i < mpRunner->num_global_tags(); ++i) {
			tags.Add(FString(mpRunner->get_global_tag(i)));
		}
		mgTags->Initialize(tags);
	}
	return mgTags;
}

void UInkThread::Resume() { mnYieldCounter--; }

void UInkThread::RegisterTagFunction(FName functionName, const FTagFunctionDelegate& function)
{
	// Register tag function
	mTagFunctions.FindOrAdd(functionName).Add(function);
}

void UInkThread::RegisterExternalFunction(
    const FString& functionName, const FExternalFunctionDelegate& function, bool lookaheadSafe
)
{
	mpRunner->bind_delegate(ink::hash_string(TCHAR_TO_ANSI(*functionName)), function, lookaheadSafe);
}

void UInkThread::RegisterExternalEvent(
    const FString& functionName, const FExternalFunctionVoidDelegate& function, bool lookaheadSafe
)
{
	mpRunner->bind_delegate(ink::hash_string(TCHAR_TO_ANSI(*functionName)), function, lookaheadSafe);
}

void UInkThread::Initialize(FString path, AInkRuntime* runtime, ink::runtime::runner thread)
{
	inkAssert(
	    ! thread->has_choices() || path.IsEmpty(),
	    "Snapshot recovery does not work with starting path if currently at choice."
	);
	mStartPath    = path;
	mpRuntime     = runtime;
	mbInitialized = true;
	mpRunner      = thread;
	mpTags        = NewObject<UTagList>();
	mkTags        = NewObject<UTagList>();
	mgTags        = NewObject<UTagList>();
	mTagFunctions.Reset();
	mCurrentChoices.Reset();
	mnChoiceToChoose = -1;
	mbHasRun         = false;
	mbInChoice       = thread->has_choices();

	OnStartup();
}

bool UInkThread::ExecuteInternal()
{
	// Kill thread
	if (mbKill)
		return true;

	// If this is the first time we're running, start us off at our starting path
	if (! mbHasRun) {
		if (! ensureMsgf(mbInitialized, TEXT("Thread executing without call to ::Initialize")))
			return true;
		mbHasRun = true;
		if (mStartPath.Len()) {
			mpRunner->move_to(ink::hash_string(TCHAR_TO_ANSI(*mStartPath)));
		}

		if (mbInChoice) {
			for (ink::size_t i = 0; i < mpRunner->num_choices(); i++) {
				UInkChoice* choice = NewObject<UInkChoice>(this);
				choice->Initialize(mpRunner->get_choice(i));
				mCurrentChoices.Add(choice);
			}
			OnChoice(mCurrentChoices);
			return false;
		}
	}

	// Execution loop
	while (true) {
		// Handle pending choice
		if (mnChoiceToChoose != -1) {
			if (ensure(mpRunner->num_choices() > 0)) {
				mpRunner->choose(mnChoiceToChoose);
			}
			mnChoiceToChoose = -1;
			mCurrentChoices.Empty();
		}

		// Execute until story yields or finishes
		while (mnYieldCounter == 0 && mpRunner->can_continue()) {
			// Handle text
			FString line = mpRunner->getline();
			// Special: Line begins with >> marker
			if (line.StartsWith(TEXT(">>"))) {
				// This is a special version of the tag function call
				// Expected: >> MyTagFunction(Arg1, Arg2, Arg3)
				FRegexPattern pattern
				    = FRegexPattern(TEXT("^>>\\s*(\\w+)(\\((\\s*(\\w+)\\s*(,\\s*(\\w+)\\s*)*)?\\))?$"));
				FRegexMatcher matcher = FRegexMatcher(pattern, line);
				if (matcher.FindNext()) {
					// Get name of function
					FString functionName = matcher.GetCaptureGroup(1);

					// Get arguments
					TArray<FString> Arguments;
					Arguments.Add(functionName);
					int groupIndex = 4;
					while (matcher.GetCaptureGroupBeginning(groupIndex) != -1) {
						Arguments.Add(matcher.GetCaptureGroup(groupIndex).TrimStartAndEnd());
						groupIndex += 2;
					}

					// Call tag method
					ExecuteTagMethod(Arguments);
				}
			} else {
				// Forward to handler
				// Get tags
				if (mpRunner->get_current_knot() != mCurrentKnot) {
					mCurrentKnot = mpRunner->get_current_knot();
					OnKnotEntered(GetGlobalTags(), GetKnotTags());
				}
				TArray<FString> tags;
				for (size_t i = 0; i < mpRunner->num_tags(); ++i) {
					tags.Add(FString(mpRunner->get_tag(i)));
				}
				mpTags->Initialize(tags);
				OnLineWritten(line, mpTags);

				// Handle tags/tag methods post-line
				for (auto it = tags.CreateConstIterator(); it; ++it) {
					// Generic tag handler
					OnTag(*it);

					// Tag methods
					TArray<FString> params;
					it->ParseIntoArray(params, TEXT("_"));
					ExecuteTagMethod(params);
				}
			}
		}

		// Handle choice block
		if (mnYieldCounter == 0 && mpRunner->num_choices() > 0) {
			mbInChoice = true;

			// Forward to handler
			for (ink::size_t i = 0; i < mpRunner->num_choices(); i++) {
				UInkChoice* choice = NewObject<UInkChoice>(this);
				choice->Initialize(mpRunner->get_choice(i));
				mCurrentChoices.Add(choice);
			}
			OnChoice(mCurrentChoices);

			// If we've chosen a choice already, go back up to handle it.
			if (mnChoiceToChoose != -1)
				continue;
		}

		break;
	}

	// Have we reached the end? If so, destroy the thread
	if (! mpRunner->can_continue() && mnYieldCounter == 0 && mpRunner->num_choices() == 0) {
		UE_LOG(InkCpp, Display, TEXT("Destroying thread"));

		// TODO: Event for ending?

		// TODO: Destroy ourselves?
		return true;
	}

	// There's more to go. Return false to put us on the waiting list.
	return false;
}

void UInkThread::ExecuteTagMethod(const TArray<FString>& Params)
{
	// Look for method and execute with parameters
	FTagFunctionMulticastDelegate* function = mTagFunctions.Find(FName(*Params[0]));
	if (function != nullptr) {
		function->Broadcast(this, Params);
	}

	// Forward to runtime
	mpRuntime->HandleTagFunction(this, Params);
}

bool UInkThread::Execute()
{
	// Execute thread
	bool finished = ExecuteInternal();

	// If we've finished, run callback
	if (finished) {
		// Allow outsiders to subscribe
		// TODO: OnThreadShutdown.Broadcast();
		OnShutdown();
	}

	// Return result
	return finished;
}

bool UInkThread::PickChoice(int index)
{
	if (index >= mCurrentChoices.Num() || index < 0) {
		UE_LOG(
		    InkCpp, Warning, TEXT("PickChoice: index(%i) out of range [0-%i)"), index,
		    mCurrentChoices.Num()
		);
		return false;
	}
	mnChoiceToChoose = index;
	mbInChoice       = false;
	return true;
}

bool UInkThread::CanExecute() const { return (mnYieldCounter == 0 && ! mbInChoice) || ! mbHasRun; }

void UInkThread::Stop() { mbKill = true; }
