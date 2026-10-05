import lynx_editor as lynx
with lynx.undo_group("Supprimer la pièce"):
    try:
        lynx.actor.delete(id="Coin")
        print("La pièce a été supprimée.")
    except Exception as e:
        print(f"Erreur : {e}")
