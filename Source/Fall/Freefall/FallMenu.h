// Minimal pause menu (Esc / Start): continue, restart the segment, controls, quit. The only place with text.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class APlayerController;

class FALL_API SFallMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SFallMenu) {}
		SLATE_ARGUMENT(TWeakObjectPtr<APlayerController>, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

	/** Opens or closes the menu for a player; pauses the game while open. */
	static void Toggle(APlayerController* PC);
	static bool IsOpen();

private:
	TSharedRef<SWidget> MakeButton(const FText& Label, TFunction<void()> OnClick);
	TSharedRef<SWidget> MakeControls();
	void Close();

	TWeakObjectPtr<APlayerController> Owner;
	TSharedPtr<class SWidgetSwitcher> Pages;
	TSharedPtr<SWidget> FirstButton;
};
