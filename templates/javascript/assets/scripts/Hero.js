// A playable character without C++ : Humanoid (engine actor) moved by the input.
// Place a "Hero" in the level, set Auto Possess Player to 0, add a Camera :
//   this.addComponent("Camera", { offset: vec3(0, 2, 80), followSpeed: 4 });
class Hero extends Humanoid {
    BeginPlay() {
        this.addComponent("Camera", { offset: vec3(0, 2, 80), followSpeed: 4 });
    }

    ProcessInput(player) {
        this.Move(Input.axis("MoveRight"));
        if (Input.pressed("Jump")) this.Jump();
        if (Input.released("Jump")) this.StopJumping();
    }
}
