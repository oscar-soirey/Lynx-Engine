// Interfaces of the game. Engine / plugin interfaces used too :
//   "Interactable"   Interact(instigator), CanInteract(instigator)   (engine)
//   "DialogueEvents" OnDialogueStarted / OnDialogueEvent / OnDialogueEnded (plugin Dialogue)
//   "StoryEvents"    OnQuestChanged(quest, state)                     (plugin Dialogue)
//   "SequenceEvents" OnSequenceFinished(sequence)                     (plugin CineCamera)

// Shows a short message to the player.
class Notifiable extends Interface {
    Notify(text) {}
}
