#include "FallMenu.h"
#include "FallDirector.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Images/SImage.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"

#define LOCTEXT_NAMESPACE "FallMenu"

namespace
{
	TSharedPtr<SFallMenu> GMenu;
	FSlateFontInfo Font(int32 Size, const char* Style = "Regular") { return FCoreStyle::GetDefaultFontStyle(Style, Size); }
	const FLinearColor Ink(0.92f, 0.93f, 0.95f), Dim(0.6f, 0.63f, 0.68f), Accent(1.f, 0.42f, 0.08f);
}

bool SFallMenu::IsOpen() { return GMenu.IsValid(); }

void SFallMenu::Toggle(APlayerController* PC)
{
	if (!PC || !GEngine || !GEngine->GameViewport) return;
	if (GMenu.IsValid())
	{
		GMenu->Close();
		return;
	}
	SAssignNew(GMenu, SFallMenu).Owner(PC);
	GEngine->GameViewport->AddViewportWidgetContent(GMenu.ToSharedRef(), 100);
	UGameplayStatics::SetGamePaused(PC, true);
	PC->bShowMouseCursor = true;
	FInputModeUIOnly Mode;
	Mode.SetWidgetToFocus(GMenu);
	PC->SetInputMode(Mode);
	FSlateApplication::Get().SetAllUserFocus(GMenu->FirstButton.IsValid() ? GMenu->FirstButton : GMenu, EFocusCause::SetDirectly);
}

void SFallMenu::Close()
{
	TSharedPtr<SFallMenu> Keep = GMenu;
	GMenu.Reset();
	if (GEngine && GEngine->GameViewport && Keep.IsValid()) GEngine->GameViewport->RemoveViewportWidgetContent(Keep.ToSharedRef());
	if (APlayerController* PC = Owner.Get())
	{
		UGameplayStatics::SetGamePaused(PC, false);
		PC->bShowMouseCursor = false;
		PC->SetInputMode(FInputModeGameOnly());
	}
}

FReply SFallMenu::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey K = InKeyEvent.GetKey();
	if (K == EKeys::Escape || K == EKeys::Gamepad_Special_Right || K == EKeys::Gamepad_FaceButton_Right)
	{
		if (Pages.IsValid() && Pages->GetActiveWidgetIndex() == 1) Pages->SetActiveWidgetIndex(0);
		else Close();
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

TSharedRef<SWidget> SFallMenu::MakeButton(const FText& Label, TFunction<void()> OnClick)
{
	TSharedRef<SButton> B = SNew(SButton)
		.ButtonColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.f))
		.ContentPadding(FMargin(4.f, 6.f))
		.OnClicked_Lambda([OnClick]() { OnClick(); return FReply::Handled(); })
		[
			SNew(STextBlock).Text(Label).Font(Font(26)).ColorAndOpacity(Ink)
		];
	return B;
}

TSharedRef<SWidget> SFallMenu::MakeControls()
{
	struct FRow { FText Action, Keys, Pad; };
	const TArray<FRow> Rows = {
		{LOCTEXT("Dive", "Нырок"), LOCTEXT("DiveK", "W"), LOCTEXT("DiveP", "левый стик вперёд")},
		{LOCTEXT("Brake", "Торможение"), LOCTEXT("BrakeK", "S"), LOCTEXT("BrakeP", "левый стик назад")},
		{LOCTEXT("Slide", "Скольжение"), LOCTEXT("SlideK", "A, D"), LOCTEXT("SlideP", "левый стик влево и вправо")},
		{LOCTEXT("Look", "Камера и прицел"), LOCTEXT("LookK", "мышь"), LOCTEXT("LookP", "правый стик")},
		{LOCTEXT("Fire", "Стрельба"), LOCTEXT("FireK", "левая кнопка мыши"), LOCTEXT("FireP", "правый курок")},
		{LOCTEXT("Aim", "Прицеливание"), LOCTEXT("AimK", "правая кнопка мыши"), LOCTEXT("AimP", "левый курок")},
		{LOCTEXT("Reload", "Перезарядка"), LOCTEXT("ReloadK", "R"), LOCTEXT("ReloadP", "X")},
		{LOCTEXT("Grab", "Схватиться за обломок, отпустить"), LOCTEXT("GrabK", "E"), LOCTEXT("GrabP", "Y")},
		{LOCTEXT("Push", "Оттолкнуться от обломка"), LOCTEXT("PushK", "пробел"), LOCTEXT("PushP", "A")},
		{LOCTEXT("Kick", "Удар ногой, отобрать парашют"), LOCTEXT("KickK", "F"), LOCTEXT("KickP", "B")},
		{LOCTEXT("Menu", "Меню"), LOCTEXT("MenuK", "Esc"), LOCTEXT("MenuP", "Start")},
	};
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0, 0, 0, 18)[SNew(STextBlock).Text(LOCTEXT("Controls", "Управление")).Font(Font(30, "Bold")).ColorAndOpacity(Accent)];
	for (const FRow& R : Rows)
	{
		Box->AddSlot().AutoHeight().Padding(0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.2f)[SNew(STextBlock).Text(R.Action).Font(Font(18)).ColorAndOpacity(Ink)]
			+ SHorizontalBox::Slot().FillWidth(0.8f)[SNew(STextBlock).Text(R.Keys).Font(Font(18)).ColorAndOpacity(Dim)]
			+ SHorizontalBox::Slot().FillWidth(1.f)[SNew(STextBlock).Text(R.Pad).Font(Font(18)).ColorAndOpacity(Dim)]
		];
	}
	Box->AddSlot().AutoHeight().Padding(0, 22, 0, 0)[MakeButton(LOCTEXT("Back", "Назад"), [this]() { Pages->SetActiveWidgetIndex(0); })];
	return Box;
}

void SFallMenu::Construct(const FArguments& InArgs)
{
	Owner = InArgs._Owner;
	TSharedRef<SWidget> Continue = MakeButton(LOCTEXT("Continue", "Продолжить"), [this]() { Close(); });
	FirstButton = Continue;
	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SImage).ColorAndOpacity(FLinearColor(0.f, 0.f, 0.02f, 0.72f))]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(FMargin(120.f, 0.f))
		[
			SNew(SBox).WidthOverride(900.f)
			[
				SAssignNew(Pages, SWidgetSwitcher)
				+ SWidgetSwitcher::Slot()
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()[Continue]
					+ SVerticalBox::Slot().AutoHeight()[MakeButton(LOCTEXT("Restart", "Начать отрезок заново"), [this]()
					{
						Close();
						if (AFallDirector* D = AFallDirector::Get(Owner.Get())) D->JumpTo(D->Checkpoint);
					})]
					+ SVerticalBox::Slot().AutoHeight()[MakeButton(LOCTEXT("FromStart", "Начать падение сначала"), [this]()
					{
						Close();
						if (AFallDirector* D = AFallDirector::Get(Owner.Get())) D->JumpTo(0.f);
					})]
					+ SVerticalBox::Slot().AutoHeight()[MakeButton(LOCTEXT("ControlsBtn", "Управление"), [this]() { Pages->SetActiveWidgetIndex(1); })]
					+ SVerticalBox::Slot().AutoHeight()[MakeButton(LOCTEXT("Quit", "Выйти"), [this]()
					{
						if (APlayerController* PC = Owner.Get()) UKismetSystemLibrary::QuitGame(PC, PC, EQuitPreference::Quit, false);
					})]
				]
				+ SWidgetSwitcher::Slot()[MakeControls()]
			]
		]
	];
}

#undef LOCTEXT_NAMESPACE
