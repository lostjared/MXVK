# Stencil

Stencil demonstrates the reusable `mxvk::VK_Stencil` helper. It draws a shader-generated mask into the stencil attachment, then fills only the masked region with a second shader pass.

## Controls

- `Escape` - quit

## How It Works

The example disables the normal depth attachment for its custom pass, configures a stencil attachment through `onConfigureDepthStencilAttachments(...)`, and renders the mask and content shaders through `VK_Stencil`.

