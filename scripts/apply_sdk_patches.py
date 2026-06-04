import os
import subprocess

def apply_patches():
    patch_dir = 'patches'
    if not os.path.exists(patch_dir):
        return

    patches = [f for f in os.listdir(patch_dir) if f.endswith('.patch')]
    patches.sort()

    for patch in patches:
        patch_path = os.path.join(patch_dir, patch)
        print(f"Applying patch {patch_path} to open-x4-sdk...")
        subprocess.run(['patch', '-d', 'open-x4-sdk', '-N', '-p1', '-i', f'../{patch_path}'], check=False)

if __name__ == "__main__":
    apply_patches()
