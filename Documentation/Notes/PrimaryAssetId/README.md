### Creating Dummy Asset to Generate Pak File
##### Step 1:
Create a new Blueprint Class, derived from PrimaryDataAsset.
Lets give it a name PDA_Item.

<br/>

##### Step 2:
Create some Data Asset(s) instances so we can test it. Make sure that it is derived from PDA_Item. Lets name it DA_Item01, DA_Item02…
Added via Content Browser->Right Click->Miscellaneous->Data Asset.

<br/>

##### Step 3:
Go to Project Settings->Game->Asset Manager and then:

Add new entry to Primary Asset Types to Scan.
Set Asset Base Class to PDA_Item.
Set Primary Asset Type to to PDA_Item_C. ← Note _C.
Make sure Has Blueprint Classes is disabled.
Set directory where you stored your Data Assets eg. /Game/Items.

<br/>

##### Step 4:
Now in BP you can use functionality such as:

Get Primary Asset Id List to obtain all asset Ids so you can load them via Async Load Primary Asset List.
Use Async Load primary Asset Class and be able to select from your primary asset ids.
Other UE / your own logic.

<br/>

##### Step 5:

Now you can edit PDA files without any need to ask programmers to do it for you in C++.

Note:
If you change DA or Primary Asset Type names, you must fix references, since they will point to old ids/types.

<br/>

##### Step 6:

Confirm the pak chunk file being generated which should be in `Saved/StagedBuild/Builds]/[ProjectName]/Content/Paks`

After confirming it, override pak chunk files using [`Config/DefaultPakFileRules.ini`](../../../Config/DefaultPakFileRules.ini)

<br/>

## Reference
https://forums.unrealengine.com/t/setup-primary-assets-via-blueprint-for-asset-manager-scan/559269/7