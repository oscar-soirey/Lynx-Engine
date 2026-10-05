import lynx_editor as lynx

with lynx.undo_group("Move Player to Mushroom"):
    # Trouver les acteurs
    actors = lynx.actors()
    player = None
    mushroom = None
    
    for a in actors:
        if a.id == "Pawn":
            player = a
        elif a.id == "Mushroom":
            mushroom = a
    
    if not player or not mushroom:
        print("Player or Mushroom not found")
    else:
        # Déplacer le joueur à la position du champignon
        result = player.move_to(mushroom.location[0], mushroom.location[1], mushroom.location[2])
        print(f"Moved player to {mushroom.location}")
