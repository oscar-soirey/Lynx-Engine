import lynx_editor as lynx

# Trouver le joueur (Pawn)
actors = lynx.actors(cls="Player")
if not actors:
    print("Aucun joueur trouvé dans le niveau.")
else:
    player = actors[0]
    # Obtenir la position du joueur en coordonnées voxel
    player_cell_x, player_cell_y = lynx.voxels.world_to_cell(player.location[0], player.location[1])
    
    # Créer une plateforme de voxels sous le joueur (1 case plus bas)
    platform_y = player_cell_y - 1
    platform_x_start = player_cell_x - 2
    platform_x_end = player_cell_x + 2
    
    with lynx.undo_group("Création de plateforme sous le joueur"):
        # Remplir la zone avec des voxels (type 1 = Vert sature)
        for x in range(platform_x_start, platform_x_end + 1):
            lynx.voxel.set(x, platform_y, 1)
    
    print(f"Plateforme créée sous le joueur {player.id} aux coordonnées voxel [{platform_x_start}, {platform_y}] à [{platform_x_end}, {platform_y}]")
