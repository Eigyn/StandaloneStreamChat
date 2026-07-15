using BeatSaberMarkupLanguage;
using HMUI;

namespace SimpleBSStreamChat.UI
{
    /// <summary>Own menu screen opened from the left-panel Mods → "Stream Chat" button.</summary>
    public class SettingsFlowCoordinator : FlowCoordinator
    {
        private SettingsViewController? _view;

        public void Setup(SettingsViewController view) => _view = view;

        protected override void DidActivate(bool firstActivation, bool addedToHierarchy, bool screenSystemEnabling)
        {
            if (firstActivation)
            {
                SetTitle("Stream Chat");
                showBackButton = true;
            }
            if (addedToHierarchy && _view != null)
                ProvideInitialViewControllers(_view);
        }

        protected override void BackButtonWasPressed(ViewController topViewController)
        {
            BeatSaberUI.MainFlowCoordinator.DismissFlowCoordinator(this);
        }
    }
}
