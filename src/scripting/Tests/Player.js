//Modele stricte mais puissant type Unreal,
//un script définit une classe:
//On utilisera pas ce type dans le moteur



//Modele leger, un script possede un parent de type
//Actor* qui peut etre réutilisé sur plusieurs actors
//c'est le style retenu pour ce moteur

//integre ca avec QuickJS à #include <quickjs-ng/...>

function BeginPlay()
{
    parent.transform.position.x=5;
}
function Update(dt)
{
    parent.transform.position.x+=dt * 3.0;
}