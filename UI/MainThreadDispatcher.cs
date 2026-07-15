using System;
using System.Collections;
using System.Collections.Concurrent;
using System.Threading;
using UnityEngine;

namespace SimpleBSStreamChat.UI
{
    /// <summary>Runs callbacks on Unity's main thread from the plugin root GameObject.</summary>
    internal sealed class MainThreadDispatcher : MonoBehaviour
    {
        private readonly ConcurrentQueue<Action> _pending = new ConcurrentQueue<Action>();
        private int _mainThreadId;

        public bool IsMainThread => Thread.CurrentThread.ManagedThreadId == _mainThreadId;

        private void Awake()
        {
            _mainThreadId = Thread.CurrentThread.ManagedThreadId;
        }

        public void Enqueue(Action action)
        {
            if (action == null)
                return;
            _pending.Enqueue(action);
        }

        public void EnqueueAfterFrames(int frames, Action action)
        {
            if (action == null)
                return;
            StartCoroutine(AfterFrames(Math.Max(0, frames), action));
        }

        /// <summary>Runs <paramref name="action"/> on the main thread after a real-time
        /// delay. Used for timeouts that must stay on the main thread (e.g. the
        /// SongCore load-event wait).</summary>
        public void EnqueueDelayed(float seconds, Action action)
        {
            if (action == null)
                return;
            StartCoroutine(AfterSeconds(Mathf.Max(0f, seconds), action));
        }

        private void Update()
        {
            while (_pending.TryDequeue(out var action))
            {
                try
                {
                    action();
                }
                catch (Exception ex)
                {
                    Plugin.Log.Warn($"Main-thread callback failed: {ex.Message}");
                }
            }
        }

        private IEnumerator AfterFrames(int frames, Action action)
        {
            for (var i = 0; i < frames; i++)
                yield return null;
            Enqueue(action);
        }

        private IEnumerator AfterSeconds(float seconds, Action action)
        {
            yield return new WaitForSecondsRealtime(seconds);
            Enqueue(action);
        }
    }
}
