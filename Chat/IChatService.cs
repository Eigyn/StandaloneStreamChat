using System;

namespace SimpleBSStreamChat.Chat
{
    public interface IChatService : IDisposable
    {
        // Raised from background threads — subscribers must marshal to the main thread themselves.
        event Action<ChatMessage>? MessageReceived;
        event Action<string>? StatusChanged;

        void Start();
        void Stop();
    }
}
